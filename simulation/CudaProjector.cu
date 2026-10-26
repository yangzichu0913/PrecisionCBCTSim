#include "simulation/CudaProjector.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <mutex>

#include <cuda_runtime.h>

namespace precisioncbct::simulation {
namespace {

/// Process-wide backend preference; Auto prefers GPU.
std::atomic<int> g_backend{static_cast<int>(ComputeBackend::Auto)};

/// Last CUDA error, thread-local so worker threads do not clobber each other.
thread_local std::string g_lastError;

/// Index-space DDA (Amanatides & Woo) mirroring CpuProjector::traverseGrid exactly.
///
/// Same box clipping, same "advance all tied axes" rule, same epsilon test, so
/// CPU and GPU must agree pixel by pixel (covered by a regression test).
__device__ void traverseGridDevice(const unsigned char* labels, const int dims[3],
                                   const double origin[3], const double spacing[3],
                                   const double source[3], const double dir[3],
                                   float* accumulators, int lanes)
{
    double i0[3];
    double di[3];
    for (int axis = 0; axis < 3; ++axis)
    {
        i0[axis] = (source[axis] - origin[axis]) / spacing[axis];
        di[axis] = dir[axis] / spacing[axis];
    }

    double tEnter = 0.0;
    double tExit = DBL_MAX;
    for (int axis = 0; axis < 3; ++axis)
    {
        const double boxMax = static_cast<double>(dims[axis]);
        if (fabs(di[axis]) < 1e-12)
        {
            if (i0[axis] < 0.0 || i0[axis] > boxMax)
            {
                return;
            }
            continue;
        }

        double t1 = (0.0 - i0[axis]) / di[axis];
        double t2 = (boxMax - i0[axis]) / di[axis];
        if (t1 > t2)
        {
            const double swap = t1;
            t1 = t2;
            t2 = swap;
        }
        tEnter = fmax(tEnter, t1);
        tExit = fmin(tExit, t2);
        if (tEnter > tExit)
        {
            return;
        }
    }
    if (tExit <= tEnter)
    {
        return;
    }

    int voxel[3];
    int step[3];
    double tNext[3];
    double tDelta[3];
    for (int axis = 0; axis < 3; ++axis)
    {
        const double position = i0[axis] + tEnter * di[axis];
        int index = static_cast<int>(floor(position));
        index = index < 0 ? 0 : (index >= dims[axis] ? dims[axis] - 1 : index);
        voxel[axis] = index;

        step[axis] = (di[axis] > 0.0) ? 1 : (di[axis] < 0.0 ? -1 : 0);
        if (step[axis] == 0)
        {
            tNext[axis] = DBL_MAX;
            tDelta[axis] = DBL_MAX;
        }
        else
        {
            const double boundary =
                (step[axis] > 0) ? static_cast<double>(index + 1) : static_cast<double>(index);
            tNext[axis] = tEnter + (boundary - position) / di[axis];
            tDelta[axis] = fabs(1.0 / di[axis]);
        }
    }

    double tCurrent = tEnter;
    const int maxSteps = dims[0] + dims[1] + dims[2] + 8;

    for (int iteration = 0; iteration < maxSteps; ++iteration)
    {
        double tLeave = fmin(fmin(tNext[0], tNext[1]), fmin(tNext[2], tExit));
        if (tLeave <= tCurrent)
        {
            break;
        }

        const size_t flat =
            (static_cast<size_t>(voxel[2]) * dims[1] + voxel[1]) * dims[0] + voxel[0];
        const unsigned char label = labels[flat];
        if (label != 0)
        {
            const float lengthMm = static_cast<float>(tLeave - tCurrent);
            if (lanes <= 1)
            {
                accumulators[0] += lengthMm;
            }
            else if (label <= lanes)
            {
                // Labels are 1-based (1..lanes), matching CPU lengths[label-1].
                accumulators[label - 1] += lengthMm;
            }
        }

        tCurrent = tLeave;
        if (tLeave >= tExit)
        {
            break;
        }

        const double epsilon = 1e-9 * (1.0 + fabs(tLeave));
        bool advanced = false;
        bool leftGrid = false;
        for (int axis = 0; axis < 3; ++axis)
        {
            if (step[axis] == 0 || tNext[axis] > tLeave + epsilon)
            {
                continue;
            }
            voxel[axis] += step[axis];
            if (voxel[axis] < 0 || voxel[axis] >= dims[axis])
            {
                leftGrid = true;
                continue;
            }
            tNext[axis] += tDelta[axis];
            advanced = true;
        }

        if (leftGrid || !advanced)
        {
            break;
        }
    }
}

/// One thread per detector pixel, DDA along that ray.
///
/// 注意：几何量必须用 double3/int3 这类 POD **按值**传进来。
/// 早期把 host 上的 double[3] 首地址当指针传进去，device 读的是 host 内存，
/// 直接触发 illegal memory access（而且会被"回退 CPU"掩盖成"结果一致"）。
__global__ void pathLengthKernel(const unsigned char* labels, int3 dims, double3 origin,
                                 double3 spacing, double3 source, double3 center, double3 uAxis,
                                 double3 vAxis, int columns, int rows, double pixelSizeMm,
                                 int lanes, float* out)
{
    const int column = blockIdx.x * blockDim.x + threadIdx.x;
    const int row = blockIdx.y * blockDim.y + threadIdx.y;
    if (column >= columns || row >= rows)
    {
        return;
    }

    const double dimsLocal[3] = {static_cast<double>(dims.x), static_cast<double>(dims.y),
                                static_cast<double>(dims.z)};
    const int dimsInt[3] = {dims.x, dims.y, dims.z};
    const double originLocal[3] = {origin.x, origin.y, origin.z};
    const double spacingLocal[3] = {spacing.x, spacing.y, spacing.z};
    const double sourceLocal[3] = {source.x, source.y, source.z};
    (void)dimsLocal;

    // World coordinate of the pixel centre (same formula as ConeBeamView::pixelCenterMm).
    const double u = (static_cast<double>(column) + 0.5 - 0.5 * columns) * pixelSizeMm;
    const double v = (static_cast<double>(row) + 0.5 - 0.5 * rows) * pixelSizeMm;
    double target[3];
    target[0] = center.x + u * uAxis.x + v * vAxis.x;
    target[1] = center.y + u * uAxis.y + v * vAxis.y;
    target[2] = center.z + u * uAxis.z + v * vAxis.z;

    double direction[3];
    double norm = 0.0;
    for (int axis = 0; axis < 3; ++axis)
    {
        direction[axis] = target[axis] - sourceLocal[axis];
        norm += direction[axis] * direction[axis];
    }
    norm = sqrt(norm);
    if (norm <= 0.0)
    {
        return;
    }
    for (int axis = 0; axis < 3; ++axis)
    {
        direction[axis] /= norm;
    }

    float accumulators[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    traverseGridDevice(labels, dimsInt, originLocal, spacingLocal, sourceLocal, direction,
                       accumulators, lanes);

    const size_t pixel = static_cast<size_t>(row) * columns + column;
    for (int lane = 0; lane < lanes; ++lane)
    {
        out[pixel * lanes + lane] = accumulators[lane];
    }
}

std::string cudaErrorText(const char* what, cudaError_t status)
{
    return std::string(what) + ": " + cudaGetErrorString(status);
}

} // namespace

void setPreferredBackend(ComputeBackend backend)
{
    g_backend.store(static_cast<int>(backend));
}

ComputeBackend preferredBackend()
{
    return static_cast<ComputeBackend>(g_backend.load());
}

std::string lastCudaError()
{
    return g_lastError;
}

void setLastCudaError(const std::string& message)
{
    g_lastError = message;
}

bool cudaAvailable()
{
    static const bool available = []() {
        int deviceCount = 0;
        if (cudaGetDeviceCount(&deviceCount) != cudaSuccess || deviceCount <= 0)
        {
            return false;
        }
        cudaDeviceProp properties{};
        if (cudaGetDeviceProperties(&properties, 0) != cudaSuccess)
        {
            return false;
        }
        // Require compute capability 7.0 or newer.
        return (properties.major * 10 + properties.minor) >= 70;
    }();
    return available;
}

std::string cudaDeviceDescription()
{
    if (!cudaAvailable())
    {
        return {};
    }
    cudaDeviceProp properties{};
    if (cudaGetDeviceProperties(&properties, 0) != cudaSuccess)
    {
        return {};
    }
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%s (sm_%d%d, %d MB)", properties.name, properties.major,
                  properties.minor,
                  static_cast<int>(properties.totalGlobalMem / (1024 * 1024)));
    return buffer;
}

CudaPathLengthSolver::CudaPathLengthSolver() = default;

CudaPathLengthSolver::~CudaPathLengthSolver()
{
    release();
}

void CudaPathLengthSolver::release()
{
    if (m_deviceLabels != nullptr)
    {
        cudaFree(m_deviceLabels);
        m_deviceLabels = nullptr;
    }
    if (m_deviceOutput != nullptr)
    {
        cudaFree(m_deviceOutput);
        m_deviceOutput = nullptr;
    }
    m_outputCapacity = 0;
    m_valid = false;
}

bool CudaPathLengthSolver::initializeGrid(const CudaGridInput& grid, std::string* error)
{
    release();

    if (!grid.isValid())
    {
        if (error) { *error = "invalid voxel grid, cannot upload to GPU"; }
        return false;
    }
    if (!cudaAvailable())
    {
        if (error) { *error = "no usable CUDA device"; }
        return false;
    }

    const cudaError_t deviceStatus = cudaSetDevice(0);
    if (deviceStatus != cudaSuccess)
    {
        if (error) { *error = cudaErrorText("cudaSetDevice failed", deviceStatus); }
        return false;
    }

    std::copy(std::begin(grid.dimensions), std::end(grid.dimensions), m_dimensions);
    std::copy(std::begin(grid.origin), std::end(grid.origin), m_origin);
    std::copy(std::begin(grid.spacingMm), std::end(grid.spacingMm), m_spacing);

    const size_t voxelCount = static_cast<size_t>(m_dimensions[0]) * m_dimensions[1]
                              * m_dimensions[2];
    const cudaError_t allocation =
        cudaMalloc(&m_deviceLabels, voxelCount * sizeof(unsigned char));
    if (allocation != cudaSuccess)
    {
        if (error) { *error = cudaErrorText("cudaMalloc failed (voxel mask)", allocation); }
        return false;
    }

    const cudaError_t copy =
        cudaMemcpy(m_deviceLabels, grid.labels, voxelCount, cudaMemcpyHostToDevice);
    if (copy != cudaSuccess)
    {
        if (error) { *error = cudaErrorText("uploading voxel mask failed", copy); }
        release();
        return false;
    }

    m_valid = true;
    return true;
}

bool CudaPathLengthSolver::compute(const ConeBeamView& view, int materialLanes,
                                   std::vector<float>* out, std::string* error)
{
    if (!m_valid || m_deviceLabels == nullptr)
    {
        if (error) { *error = "CUDA solver is not initialised"; }
        return false;
    }
    if (out == nullptr)
    {
        if (error) { *error = "internal error: null output buffer"; }
        return false;
    }

    ConeBeamView resolved = view;
    if (!resolved.update())
    {
        if (error) { *error = "invalid projection geometry"; }
        return false;
    }

    const int lanes = std::clamp(materialLanes, 1, 4);
    const int columns = resolved.columns;
    const int rows = resolved.rows;
    const size_t pixelCount = static_cast<size_t>(columns) * static_cast<size_t>(rows);
    const size_t required = pixelCount * static_cast<size_t>(lanes);

    if (required > m_outputCapacity)
    {
        if (m_deviceOutput != nullptr)
        {
            cudaFree(m_deviceOutput);
            m_deviceOutput = nullptr;
            m_outputCapacity = 0;
        }
        const cudaError_t allocation = cudaMalloc(&m_deviceOutput, required * sizeof(float));
        if (allocation != cudaSuccess)
        {
            if (error) { *error = cudaErrorText("cudaMalloc failed (output)", allocation); }
            return false;
        }
        m_outputCapacity = required;
    }

    const auto* deviceLabels = static_cast<const unsigned char*>(m_deviceLabels);
    auto* deviceOutput = static_cast<float*>(m_deviceOutput);

    // Geometry passed by value as POD vector types (a host array would decay to a pointer!)
    const int3 dimsLocal = make_int3(m_dimensions[0], m_dimensions[1], m_dimensions[2]);
    const double3 originLocal = make_double3(m_origin[0], m_origin[1], m_origin[2]);
    const double3 spacingLocal = make_double3(m_spacing[0], m_spacing[1], m_spacing[2]);
    const double3 sourceLocal = make_double3(resolved.sourcePositionMm[0],
                                             resolved.sourcePositionMm[1],
                                             resolved.sourcePositionMm[2]);
    const double3 centerLocal = make_double3(resolved.detectorCenterMm[0],
                                             resolved.detectorCenterMm[1],
                                             resolved.detectorCenterMm[2]);
    const double3 uLocal = make_double3(resolved.detectorUAxis[0], resolved.detectorUAxis[1],
                                        resolved.detectorUAxis[2]);
    const double3 vLocal = make_double3(resolved.detectorVAxis[0], resolved.detectorVAxis[1],
                                        resolved.detectorVAxis[2]);

    const dim3 block(16, 16);
    const dim3 grid((columns + block.x - 1) / block.x, (rows + block.y - 1) / block.y);

    pathLengthKernel<<<grid, block>>>(deviceLabels, dimsLocal, originLocal, spacingLocal,
                                      sourceLocal, centerLocal, uLocal, vLocal, columns, rows,
                                      resolved.pixelSizeMm, lanes, deviceOutput);

    const cudaError_t launch = cudaGetLastError();
    if (launch != cudaSuccess)
    {
        if (error) { *error = cudaErrorText("kernel launch failed", launch); }
        return false;
    }
    const cudaError_t sync = cudaDeviceSynchronize();
    if (sync != cudaSuccess)
    {
        if (error) { *error = cudaErrorText("kernel execution failed", sync); }
        return false;
    }

    out->resize(required);
    const cudaError_t download =
        cudaMemcpy(out->data(), deviceOutput, required * sizeof(float), cudaMemcpyDeviceToHost);
    if (download != cudaSuccess)
    {
        if (error) { *error = cudaErrorText("downloading results failed", download); }
        return false;
    }

    // 成功路径清掉历史错误，避免调用方把旧的失败信息当成这次的
    g_lastError.clear();
    return true;
}

} // namespace precisioncbct::simulation
