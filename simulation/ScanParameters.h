#pragma once

#include <cmath>
#include <cstdint>

#include "simulation/AttenuationTable.h"
#include "simulation/DetectorModelParams.h"

namespace precisioncbct::simulation {

/// 能谱与噪声设置（Phase 3）。
struct SpectralSettings
{
    bool enabled = false;                       ///< true 时用多能谱 + 噪声模型替代单一 μ
    double tubeVoltageKvp = 100.0;              ///< 管电压
    double inherentFiltrationAlMm = 2.5;        ///< 铝滤过厚度
    double incidentPhotonsPerPixel = 1.0e5;     ///< 每像素入射光子数（决定量子噪声水平）
    bool enableNoise = true;                    ///< Poisson 量子噪声
    std::uint32_t seed = 20261026u;             ///< 噪声随机种子（固定值保证结果可复现）
    Material material = Material::Water;        ///< 体素网格的材料（当前整块单一材料）
    DetectorModelOptions detector;              ///< 探测器与散射模型（全 0 = 不启用）
};

/// CBCT 扫描几何与探测器配置。
///
/// 术语与单位：
/// - SAD（source-axis distance）：射线源到旋转轴（等中心）的距离，mm
/// - SDD（source-detector distance）：射线源到探测器平面的距离，mm
/// - 角度：度（deg），机架绕 z 轴旋转
///
/// 这里只描述参数本身；投影几何计算与射线追踪在后续步骤实现。
struct ScanParameters
{
    double sourceAxisDistanceMm = 500.0;      ///< SAD
    double sourceDetectorDistanceMm = 1000.0; ///< SDD，必须大于 SAD
    double detectorPixelSizeMm = 0.4;         ///< 探测器像素边长（假定各向同性）
    int detectorColumns = 512;                ///< 探测器列数（u 方向）
    int detectorRows = 512;                   ///< 探测器行数（v 方向）
    double startAngleDeg = 0.0;               ///< 第一帧的机架角度
    double endAngleDeg = 359.0;               ///< 最后一帧的机架角度
    double angleStepDeg = 1.0;                ///< 角度步长

    /// 探测器在 u 方向（列）的物理宽度，mm
    double detectorWidthMm() const
    {
        return detectorColumns * detectorPixelSizeMm;
    }

    /// 探测器在 v 方向（行）的物理高度，mm
    double detectorHeightMm() const
    {
        return detectorRows * detectorPixelSizeMm;
    }

    /// 按当前角度区间与步长可得到的投影帧数；参数非法时返回 0
    int projectionCount() const
    {
        if (angleStepDeg <= 0.0)
        {
            return 0;
        }
        const double span = std::fabs(endAngleDeg - startAngleDeg);
        return static_cast<int>(std::floor(span / angleStepDeg)) + 1;
    }
};

} // namespace precisioncbct::simulation
