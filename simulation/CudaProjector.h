#pragma once

#include <string>
#include <vector>

#include "simulation/ConeBeamView.h"

namespace precisioncbct::simulation {

/// 计算后端选择。
enum class ComputeBackend
{
    Auto = 0, ///< 有可用 GPU 就走 GPU，否则 CPU
    Cpu,
    Gpu,
};

/// GPU 侧的体素网格输入（刻意不依赖 VTK，这样 .cu 只包含纯 C++ 头，
/// 可以用 nvcc 单独编译而不需要 VTK 的 include 路径）。
struct CudaGridInput
{
    const unsigned char* labels = nullptr; ///< 0 = 空气，1..4 = 材料标签
    int dimensions[3] = {0, 0, 0};
    double origin[3] = {0.0, 0.0, 0.0};
    double spacingMm[3] = {1.0, 1.0, 1.0};

    bool isValid() const
    {
        return labels != nullptr && dimensions[0] > 0 && dimensions[1] > 0 && dimensions[2] > 0
            && spacingMm[0] > 0.0 && spacingMm[1] > 0.0 && spacingMm[2] > 0.0;
    }
};

/// 设定全局后端偏好（进程级）。GPU 不可用时 Auto/Gpu 都会回退到 CPU。
void setPreferredBackend(ComputeBackend backend);
ComputeBackend preferredBackend();

/// 本次进程是否真的能用 GPU（编译期启用 + 运行期设备可用 + 驱动正常）
bool cudaAvailable();

/// 设备信息，例如 "NVIDIA GeForce RTX 4060 Laptop GPU (sm_89, 8188 MB)"；不可用时返回空串
std::string cudaDeviceDescription();

/// 上一次 GPU 调用失败的原因（便于在界面上提示，而不是静默退回 CPU）
std::string lastCudaError();
/// 记录一次 GPU 失败原因（供 CPU 回退路径使用）
void setLastCudaError(const std::string& message);

/// GPU 版路径长度求解器。
///
/// 体素掩膜只上传一次，之后每帧只需机架角与源/探测器位姿，
/// 因此非常适合多角度序列与重建（逐帧调用 compute()）。
///
/// 注意：实例绑定在创建它的线程上（CUDA 上下文是线程局部的）；
/// 后台序列线程各自持有一个实例即可。
class CudaPathLengthSolver
{
public:
    CudaPathLengthSolver();
    ~CudaPathLengthSolver();

    CudaPathLengthSolver(const CudaPathLengthSolver&) = delete;
    CudaPathLengthSolver& operator=(const CudaPathLengthSolver&) = delete;

    /// 上传体素掩膜。成功后 isValid() 为 true。
    bool initializeGrid(const CudaGridInput& grid, std::string* error);

    bool isValid() const { return m_valid; }

    /// 计算单帧各像素的路径长度。
    /// materialLanes = 1 时 out 为 width×height；>1 时按标签分别累计（width×height×lanes，
    /// 标签 1 对应 lane 0，与 CPU 版 lengthsMm[label-1] 一致）。
    bool compute(const ConeBeamView& view, int materialLanes, std::vector<float>* out,
                 std::string* error);

private:
    void release();

    bool m_valid = false;
    void* m_deviceLabels = nullptr; ///< cudaMalloc 得到的 unsigned char*
    int m_dimensions[3] = {0, 0, 0};
    double m_origin[3] = {0.0, 0.0, 0.0};
    double m_spacing[3] = {1.0, 1.0, 1.0};
    void* m_deviceOutput = nullptr; ///< float*，容量按最大像素数 ×4 分配
    size_t m_outputCapacity = 0;
};

} // namespace precisioncbct::simulation
