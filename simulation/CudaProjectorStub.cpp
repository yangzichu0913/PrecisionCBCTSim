// CUDA 未启用（或没有 CUDA 工具链）时的空实现。
//
// 全部接口都在这里保持与 CUDA 版一致的语义：cudaAvailable() 返回 false，
// 求解器初始化必然失败，于是 CpuProjector 会走 CPU 路径。
// 这样上层代码不需要任何 #ifdef。

#include "simulation/CudaProjector.h"

#include <atomic>

namespace precisioncbct::simulation {
namespace {

std::atomic<int> g_backend{static_cast<int>(ComputeBackend::Auto)};

} // namespace

void setPreferredBackend(ComputeBackend backend)
{
    g_backend.store(static_cast<int>(backend));
}

ComputeBackend preferredBackend()
{
    return static_cast<ComputeBackend>(g_backend.load());
}

bool cudaAvailable()
{
    return false;
}

std::string cudaDeviceDescription()
{
    return {};
}

std::string lastCudaError()
{
    return "本版本未启用 CUDA（构建时未找到 CUDA 工具链或已关闭 PCSIM_ENABLE_CUDA）";
}

void setLastCudaError(const std::string&)
{
}

CudaPathLengthSolver::CudaPathLengthSolver() = default;
CudaPathLengthSolver::~CudaPathLengthSolver() = default;

bool CudaPathLengthSolver::initializeGrid(const CudaGridInput&, std::string* error)
{
    if (error) { *error = lastCudaError(); }
    return false;
}

bool CudaPathLengthSolver::compute(const ConeBeamView&, int, std::vector<float>*, std::string* error)
{
    if (error) { *error = lastCudaError(); }
    return false;
}

} // namespace precisioncbct::simulation
