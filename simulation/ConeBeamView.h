#pragma once

#include <array>

namespace precisioncbct::simulation {

/// 单个投影视角下的锥形束几何。
///
/// 坐标约定（全项目统一）：
/// - 旋转轴为 **z 轴**，等中心（isocenter）在原点；
/// - 机架角 angleDeg = 0 时，射线源位于 (0, -SAD, 0)，探测器中心位于 (0, SDD-SAD, 0)；
/// - 角度增大表示绕 +z 轴逆时针旋转（从 +z 俯视）；
/// - 探测器 u 轴（列方向）在 xy 平面内，v 轴（行方向）沿 +z。
/// 单位：长度 mm，角度 deg。
struct ConeBeamView
{
    double sourceAxisDistanceMm = 500.0;      ///< SAD：源到等中心
    double sourceDetectorDistanceMm = 1000.0; ///< SDD：源到探测器平面
    double pixelSizeMm = 0.4;                 ///< 探测器像素边长（各向同性）
    int columns = 512;                        ///< u 方向像素数
    int rows = 512;                           ///< v 方向像素数
    double angleDeg = 0.0;                    ///< 当前机架角

    // 以下为 update() 派生的量
    std::array<double, 3> sourcePositionMm{};   ///< 射线源位置
    std::array<double, 3> detectorCenterMm{};   ///< 探测器平面中心
    std::array<double, 3> detectorUAxis{};      ///< 列方向单位向量
    std::array<double, 3> detectorVAxis{};      ///< 行方向单位向量

    /// 依据上面的参数与角度重算派生量。参数非法时返回 false。
    bool update();

    /// 第 (column, row) 个像素中心的世界坐标（像素索引从 0 开始）
    std::array<double, 3> pixelCenterMm(int column, int row) const;

    /// 从射线源指向该像素中心的单位向量
    std::array<double, 3> rayDirection(int column, int row) const;

    bool isValid() const
    {
        return sourceAxisDistanceMm > 0.0
            && sourceDetectorDistanceMm > sourceAxisDistanceMm
            && pixelSizeMm > 0.0
            && columns > 0 && rows > 0;
    }
};

} // namespace precisioncbct::simulation
