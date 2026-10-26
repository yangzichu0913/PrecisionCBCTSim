#include "simulation/ConeBeamView.h"

#include <cmath>

namespace precisioncbct::simulation {
namespace {

constexpr double kPi = 3.14159265358979323846;

} // namespace

bool ConeBeamView::update()
{
    if (!isValid())
    {
        return false;
    }

    const double theta = angleDeg * kPi / 180.0;
    const double sinTheta = std::sin(theta);
    const double cosTheta = std::cos(theta);

    // 机架绕 z 轴旋转：0° 时源在 -y 方向
    sourcePositionMm = {sourceAxisDistanceMm * sinTheta,
                        -sourceAxisDistanceMm * cosTheta,
                        0.0};

    // 探测器位于等中心的另一侧：源在 -y 时探测器在 +y
    const double detectorOffset = sourceDetectorDistanceMm - sourceAxisDistanceMm;
    detectorCenterMm = {-detectorOffset * sinTheta, detectorOffset * cosTheta, 0.0};

    // 列方向随角度旋转；行方向始终沿 z（患者长轴）
    detectorUAxis = {cosTheta, sinTheta, 0.0};
    detectorVAxis = {0.0, 0.0, 1.0};
    return true;
}

std::array<double, 3> ConeBeamView::pixelCenterMm(int column, int row) const
{
    const double u = (static_cast<double>(column) + 0.5 - 0.5 * columns) * pixelSizeMm;
    const double v = (static_cast<double>(row) + 0.5 - 0.5 * rows) * pixelSizeMm;

    return {detectorCenterMm[0] + u * detectorUAxis[0] + v * detectorVAxis[0],
            detectorCenterMm[1] + u * detectorUAxis[1] + v * detectorVAxis[1],
            detectorCenterMm[2] + u * detectorUAxis[2] + v * detectorVAxis[2]};
}

std::array<double, 3> ConeBeamView::rayDirection(int column, int row) const
{
    const std::array<double, 3> target = pixelCenterMm(column, row);
    const double dx = target[0] - sourcePositionMm[0];
    const double dy = target[1] - sourcePositionMm[1];
    const double dz = target[2] - sourcePositionMm[2];
    const double norm = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (norm <= 0.0)
    {
        return {0.0, 0.0, 1.0};
    }
    return {dx / norm, dy / norm, dz / norm};
}

} // namespace precisioncbct::simulation
