#include "simulation/CpuProjector.h"

#include "io/VoxelGrid.h"
#include "simulation/CudaProjector.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include <vtkImageData.h>

namespace precisioncbct::simulation {
namespace {

/// 按行把任务分给多个线程（每行像素互相独立，写入各自区间，无需加锁）。
template <typename RowFn>
void parallelForRows(int rowCount, RowFn&& rowFn)
{
    unsigned int threadCount = std::thread::hardware_concurrency();
    if (threadCount == 0)
    {
        threadCount = 1;
    }
    threadCount = std::min<unsigned int>(threadCount, static_cast<unsigned int>(std::max(rowCount, 1)));

    if (threadCount <= 1)
    {
        rowFn(0, rowCount);
        return;
    }

    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    const int rowsPerThread = (rowCount + static_cast<int>(threadCount) - 1)
                              / static_cast<int>(threadCount);
    for (unsigned int i = 0; i < threadCount; ++i)
    {
        const int firstRow = static_cast<int>(i) * rowsPerThread;
        const int lastRow = std::min(firstRow + rowsPerThread, rowCount);
        if (firstRow >= lastRow)
        {
            break;
        }
        workers.emplace_back(rowFn, firstRow, lastRow);
    }
    for (std::thread& worker : workers)
    {
        worker.join();
    }
}

/// 每线程缓存一个 GPU 求解器：体素掩膜只上传一次，序列/重建逐帧复用。
/// 用 thread_local 是因为 CUDA 上下文是线程局部的（后台序列线程各自持有一份）。
struct GpuSolverCache
{
    const void* imageKey = nullptr;
    int dimensions[3] = {0, 0, 0};
    std::unique_ptr<CudaPathLengthSolver> solver;
    std::vector<float> buffer;
};

GpuSolverCache& gpuSolverCache()
{
    static thread_local GpuSolverCache cache;
    return cache;
}

bool shouldUseGpu()
{
    const ComputeBackend backend = preferredBackend();
    if (backend == ComputeBackend::Cpu)
    {
        return false;
    }
    return cudaAvailable();
}

/// 尝试用 GPU 计算路径长度；成功时把 width×height×lanes 的扁平结果写进 buf。
/// 失败时返回 false，并把原因记到 lastCudaError()，调用方回退 CPU。
bool gpuPathLengths(const io::VoxelGrid& grid, const ConeBeamView& resolvedView, int lanes,
                    std::vector<float>* buf, std::string* error)
{
    double origin[3];
    grid.image->GetOrigin(origin);

    CudaGridInput input;
    input.labels = static_cast<const unsigned char*>(grid.image->GetScalarPointer());
    std::copy(std::begin(grid.dimensions), std::end(grid.dimensions), input.dimensions);
    std::copy(std::begin(origin), std::end(origin), input.origin);
    std::copy(std::begin(grid.spacingMm), std::end(grid.spacingMm), input.spacingMm);

    GpuSolverCache& cache = gpuSolverCache();
    const bool keyMatches = cache.solver != nullptr && cache.imageKey == grid.image.GetPointer()
                            && cache.dimensions[0] == grid.dimensions[0]
                            && cache.dimensions[1] == grid.dimensions[1]
                            && cache.dimensions[2] == grid.dimensions[2];
    if (!keyMatches)
    {
        cache.solver = std::make_unique<CudaPathLengthSolver>();
        std::string initError;
        if (!cache.solver->initializeGrid(input, &initError))
        {
            setLastCudaError(initError);
            if (error) { *error = initError; }
            cache.solver.reset();
            cache.imageKey = nullptr;
            return false;
        }
        cache.imageKey = grid.image.GetPointer();
        std::copy(std::begin(grid.dimensions), std::end(grid.dimensions), cache.dimensions);
    }

    if (!cache.solver->compute(resolvedView, lanes, &cache.buffer, error))
    {
        setLastCudaError(error != nullptr ? *error : std::string("未知 CUDA 错误"));
        cache.solver.reset();
        cache.imageKey = nullptr;
        return false;
    }

    *buf = cache.buffer;
    setLastCudaError(std::string());
    return true;
}

/// 射线在规则网格中的索引空间 DDA 遍历（Amanatides & Woo）。///
/// labels 为 dims[0]*dims[1]*dims[2] 的体素标签（0 = 空气），
/// dir 为世界坐标下的单位向量，因此 t 的增量直接等于毫米数。
/// 每个体素调用 add(label, 该体素内的穿行长度)：用回调而非返回值，
/// 让「二值掩膜累计总长度」与「多材料分别累计」共用同一份遍历代码。
template <typename AddLength>
void traverseGrid(const unsigned char* labels,
                  const int dims[3],
                  const double origin[3],
                  const double spacing[3],
                  const std::array<double, 3>& source,
                  const std::array<double, 3>& dir,
                  AddLength&& add)
{
    double i0[3];
    double di[3];
    for (int axis = 0; axis < 3; ++axis)
    {
        i0[axis] = (source[axis] - origin[axis]) / spacing[axis];
        di[axis] = dir[axis] / spacing[axis];
    }

    // 先与网格包围盒求交，得到 [tEnter, tExit]
    double tEnter = 0.0;
    double tExit = std::numeric_limits<double>::max();
    for (int axis = 0; axis < 3; ++axis)
    {
        const double boxMax = static_cast<double>(dims[axis]);
        if (std::fabs(di[axis]) < 1e-12)
        {
            if (i0[axis] < 0.0 || i0[axis] > boxMax)
            {
                return; // 射线平行于该轴且在盒外
            }
            continue;
        }

        double t1 = (0.0 - i0[axis]) / di[axis];
        double t2 = (boxMax - i0[axis]) / di[axis];
        if (t1 > t2)
        {
            std::swap(t1, t2);
        }
        tEnter = std::max(tEnter, t1);
        tExit = std::min(tExit, t2);
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
        int index = static_cast<int>(std::floor(position));
        index = std::clamp(index, 0, dims[axis] - 1);
        voxel[axis] = index;

        if (di[axis] > 0.0)
        {
            step[axis] = 1;
        }
        else if (di[axis] < 0.0)
        {
            step[axis] = -1;
        }
        else
        {
            step[axis] = 0;
        }

        if (step[axis] == 0)
        {
            tNext[axis] = std::numeric_limits<double>::max();
            tDelta[axis] = std::numeric_limits<double>::max();
        }
        else
        {
            const double boundary = (step[axis] > 0) ? static_cast<double>(index + 1)
                                                     : static_cast<double>(index);
            tNext[axis] = tEnter + (boundary - position) / di[axis];
            tDelta[axis] = std::fabs(1.0 / di[axis]);
        }
    }

    double tCurrent = tEnter;
    const int maxSteps = dims[0] + dims[1] + dims[2] + 8;

    for (int iteration = 0; iteration < maxSteps; ++iteration)
    {
        const double tLeave = std::min({tNext[0], tNext[1], tNext[2], tExit});
        if (tLeave <= tCurrent)
        {
            break;
        }

        const size_t flat = (static_cast<size_t>(voxel[2]) * dims[1] + voxel[1]) * dims[0] + voxel[0];
        add(labels[flat], tLeave - tCurrent);

        tCurrent = tLeave;
        if (tLeave >= tExit)
        {
            break;
        }

        // 射线穿过体素棱/角时多个轴的边界时间并列，必须一起前进；
        // 否则未前进的轴仍停在 tCurrent，下一轮会被判定为退化并提前结束遍历。
        const double epsilon = 1e-9 * (1.0 + std::fabs(tLeave));
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

} // namespace

PathLengthField computePathLengths(const io::VoxelGrid& grid,
                                   const ConeBeamView& view,
                                   std::string* error)
{
    PathLengthField field;

    if (!grid.isValid())
    {
        if (error) { *error = "体素网格无效，请先完成体素化"; }
        return field;
    }

    // 用副本重算派生量，调用方不必先手动 update()
    field.resolvedView = view;
    if (!field.resolvedView.update())
    {
        if (error) { *error = "投影几何参数非法（需满足 0 < SAD < SDD、像素尺寸 > 0、行列数 > 0）"; }
        return field;
    }

    const auto* labels = static_cast<const unsigned char*>(grid.image->GetScalarPointer());
    double origin[3];
    grid.image->GetOrigin(origin);

    const ConeBeamView& currentView = field.resolvedView;
    field.width = currentView.columns;
    field.height = currentView.rows;
    field.lengthsMm.assign(static_cast<size_t>(field.width) * static_cast<size_t>(field.height), 0.0f);

    // GPU 优先（可用时）；失败自动回退 CPU，并把原因留在 lastCudaError()
    if (shouldUseGpu())
    {
        std::vector<float> buffer;
        std::string gpuError;
        if (gpuPathLengths(grid, currentView, 1, &buffer, &gpuError)
            && buffer.size() == field.lengthsMm.size())
        {
            std::copy(buffer.begin(), buffer.end(), field.lengthsMm.begin());
            return field;
        }
        setLastCudaError(gpuError.empty() ? std::string("GPU 计算失败") : gpuError);
    }

    // CPU 版（也是 GPU 不可用时的回退路径）：总工作量 = 像素数 × 平均穿越体素数。
    parallelForRows(field.height, [&](int firstRow, int lastRow) {
        for (int row = firstRow; row < lastRow; ++row)
        {
            for (int column = 0; column < currentView.columns; ++column)
            {
                const size_t index = static_cast<size_t>(row) * currentView.columns + column;
                const std::array<double, 3> direction = currentView.rayDirection(column, row);
                double totalMm = 0.0;
                traverseGrid(labels, grid.dimensions, origin, grid.spacingMm,
                             currentView.sourcePositionMm, direction,
                             [&totalMm](unsigned char label, double lengthMm) {
                                 if (label != 0)
                                 {
                                     totalMm += lengthMm;
                                 }
                             });
                field.lengthsMm[index] = static_cast<float>(totalMm);
            }
        }
    });

    return field;
}

MaterialPathLengthField computeMaterialPathLengths(const io::VoxelGrid& labeledGrid,
                                                   const ConeBeamView& view,
                                                   int maxLabels,
                                                   std::string* error)
{
    MaterialPathLengthField field;

    if (!labeledGrid.isValid())
    {
        if (error) { *error = "带材料标签的体素网格无效"; }
        return field;
    }
    if (maxLabels < 1 || maxLabels > kMaxTrackedMaterials)
    {
        if (error) { *error = "材料标签数量超出支持范围（1..4）"; }
        return field;
    }

    field.resolvedView = view;
    if (!field.resolvedView.update())
    {
        if (error) { *error = "投影几何参数非法（需满足 0 < SAD < SDD、像素尺寸 > 0、行列数 > 0）"; }
        return field;
    }

    const auto* labels = static_cast<const unsigned char*>(labeledGrid.image->GetScalarPointer());
    double origin[3];
    labeledGrid.image->GetOrigin(origin);
    const int dims[3] = {labeledGrid.dimensions[0], labeledGrid.dimensions[1],
                         labeledGrid.dimensions[2]};
    const double spacing[3] = {labeledGrid.spacingMm[0], labeledGrid.spacingMm[1],
                               labeledGrid.spacingMm[2]};

    const ConeBeamView& currentView = field.resolvedView;
    field.width = currentView.columns;
    field.height = currentView.rows;
    field.labelCount = maxLabels;
    field.lengthsMm.assign(static_cast<size_t>(field.width) * static_cast<size_t>(field.height),
                           std::array<float, kMaxTrackedMaterials>{0.0f, 0.0f, 0.0f, 0.0f});

    if (shouldUseGpu())
    {
        std::vector<float> buffer;
        std::string gpuError;
        if (gpuPathLengths(labeledGrid, currentView, maxLabels, &buffer, &gpuError)
            && buffer.size() == field.lengthsMm.size() * static_cast<size_t>(maxLabels))
        {
            for (size_t i = 0; i < field.lengthsMm.size(); ++i)
            {
                for (int lane = 0; lane < maxLabels; ++lane)
                {
                    field.lengthsMm[i][static_cast<size_t>(lane)] =
                        buffer[i * static_cast<size_t>(maxLabels) + static_cast<size_t>(lane)];
                }
            }
            return field;
        }
        setLastCudaError(gpuError.empty() ? std::string("GPU 计算失败") : gpuError);
    }

    parallelForRows(field.height, [&](int firstRow, int lastRow) {
        for (int row = firstRow; row < lastRow; ++row)
        {
            for (int column = 0; column < currentView.columns; ++column)
            {
                const size_t index = static_cast<size_t>(row) * currentView.columns + column;
                const std::array<double, 3> direction = currentView.rayDirection(column, row);
                auto& lengths = field.lengthsMm[index];
                traverseGrid(labels, dims, origin, spacing, currentView.sourcePositionMm, direction,
                             [&lengths, maxLabels](unsigned char label, double lengthMm) {
                                 if (label == 0 || label > maxLabels)
                                 {
                                     return;
                                 }
                                 lengths[label - 1] += static_cast<float>(lengthMm);
                             });
            }
        }
    });

    return field;
}

DrProjection projectConeBeam(const io::VoxelGrid& grid,
                             const ConeBeamView& view,
                             const ProjectionParameters& parameters,
                             std::string* error)
{
    DrProjection projection;

    if (!(parameters.attenuationPerMm > 0.0))
    {
        if (error) { *error = "线衰减系数必须大于 0"; }
        return projection;
    }

    const PathLengthField field = computePathLengths(grid, view, error);
    if (!field.isValid())
    {
        return projection;
    }

    projection.width = field.width;
    projection.height = field.height;
    const size_t pixelCount = field.lengthsMm.size();
    projection.lineIntegralMm = field.lengthsMm;
    projection.transmission.assign(pixelCount, 1.0f);

    const double mu = parameters.attenuationPerMm;
    for (size_t index = 0; index < pixelCount; ++index)
    {
        const double lengthMm = field.lengthsMm[index];
        projection.transmission[index] = static_cast<float>(
            parameters.incidentIntensity * std::exp(-mu * lengthMm));
    }

    // 统计量用于显示归一化与自检
    const auto maxIt = std::max_element(projection.lineIntegralMm.begin(),
                                        projection.lineIntegralMm.end());
    const auto minTIt = std::min_element(projection.transmission.begin(),
                                         projection.transmission.end());
    projection.maxLineIntegralMm = (maxIt != projection.lineIntegralMm.end()) ? *maxIt : 0.0f;
    projection.minTransmission = (minTIt != projection.transmission.end()) ? *minTIt : 1.0f;
    return projection;
}

} // namespace precisioncbct::simulation
