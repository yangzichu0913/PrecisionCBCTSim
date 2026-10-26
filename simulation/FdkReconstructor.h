#pragma once

#include <array>
#include <string>
#include <vector>

#include <vtkSmartPointer.h>

#include "simulation/ConeBeamView.h"
#include "simulation/CpuProjector.h"

class vtkImageData;

namespace precisioncbct::simulation {

/// 重建出来的断层体数据（线衰减系数 μ，单位 1/mm）。
struct ReconstructedVolume
{
    int dimensions[3] = {0, 0, 0};
    double spacingMm[3] = {1.0, 1.0, 1.0};
    double originMm[3] = {0.0, 0.0, 0.0};
    std::vector<float> attenuationPerMm; ///< 行优先（z 最外层）

    bool isValid() const
    {
        return dimensions[0] > 0 && dimensions[1] > 0 && dimensions[2] > 0
            && attenuationPerMm.size()
                   == static_cast<size_t>(dimensions[0]) * dimensions[1] * dimensions[2];
    }

    const float& at(int i, int j, int k) const
    {
        return attenuationPerMm[(static_cast<size_t>(k) * dimensions[1] + j) * dimensions[0] + i];
    }

    /// 体素中心的世界坐标（mm）
    std::array<double, 3> voxelCenterMm(int i, int j, int k) const
    {
        return {originMm[0] + (i + 0.5) * spacingMm[0],
                originMm[1] + (j + 0.5) * spacingMm[1],
                originMm[2] + (k + 0.5) * spacingMm[2]};
    }

    double minValue() const;
    double maxValue() const;

    /// 转成 VTK 体数据（标量类型 float，便于三向切面显示）
    vtkSmartPointer<vtkImageData> toImageData() const;
};

/// FDK（Feldkamp-Davis-Kress）锥束重建参数。
struct FdkOptions
{
    /// 重建体积的体素数（立方，建议 128~512）
    int volumeSize = 128;
    /// 体素边长（mm）
    double voxelSizeMm = 1.0;
    /// 重建体积中心（世界坐标 mm），默认等中心原点
    double centerMm[3] = {0.0, 0.0, 0.0};
    /// 是否加 Hann 窗（抑制高频噪声，代价是空间分辨率下降）
    bool hannWindow = false;
};

/// 锥束 FDK 重建（CPU、逐帧流式累加）。
///
/// 用法：先用与投影完全相同的几何构造，然后按序列顺序把每一帧喂进来，
/// 最后取 volume()。流式设计是刻意的——360 帧 512² 的投影约 380 MB，
/// 全部留在内存里会挤掉体素与界面内存。
///
/// 算法（Feldkamp 1984，平板探测器）：
///   1. p(u,v) = −ln(T)                      透射率 → 线积分（无量纲）
///   2. p_w = p · D/√(D²+u²+v²)              余弦加权（D = SDD）
///   3. q = ramp 滤波（Ram-Lak，频域 |f|）    沿 u 方向逐行滤波
///   4. f(x,y,z) = (Δβ/2)·Σ_β (R/(R+y'))²·q(u,v)
///      其中 R = SAD，y' 是体素在「源-探测器」局部坐标系下的纵向坐标，
///      u = D·x'/(R+y')、v = D·z/(R+y')。
/// 角度覆盖越完整（≥180°+扇角）重建越准；不足 180° 时会在边缘出现伪影。
class FdkReconstructor
{
public:
    FdkReconstructor(const FdkOptions& options, const ConeBeamView& geometry);

    bool isValid() const { return m_valid; }
    const std::string& error() const { return m_error; }

    /// 累加一帧投影。angleDeg 必须与该帧投影对应的机架角一致。
    /// 返回 false 表示该帧被跳过（几何非法或投影尺寸不一致）。
    bool addProjection(double angleDeg, const DrProjection& projection);

    /// 当前累加了多少帧、累计覆盖的角度（用于判断是否够 180°）
    int projectionCount() const { return m_projectionCount; }
    double accumulatedAngleDeg() const { return m_accumulatedAngleDeg; }

    /// 取出重建结果（会做最后的 Δβ/2 缩放；多次调用返回同一结果）
    ReconstructedVolume volume() const;

    void reset();

private:
    /// 沿 u 方向对一行数据做 ramp 滤波（频域 |f|，可选 Hann 窗）
    void rampFilterRow(const std::vector<double>& input, std::vector<double>* output) const;

    FdkOptions m_options;
    ConeBeamView m_geometry;
    bool m_valid = false;
    std::string m_error;

    int m_columns = 0;
    int m_rows = 0;
    double m_pixelSizeMm = 0.0;
    double m_sadMm = 0.0;
    double m_sddMm = 0.0;

    std::vector<double> m_volume; ///< 累加中的重建体积（未乘 Δβ/2）
    int m_projectionCount = 0;
    double m_accumulatedAngleDeg = 0.0;
    double m_previousAngleDeg = 0.0;
    bool m_hasPreviousAngle = false;
    double m_deltaBetaRad = 0.0; ///< 最近一次的角步长估计
};

} // namespace precisioncbct::simulation
