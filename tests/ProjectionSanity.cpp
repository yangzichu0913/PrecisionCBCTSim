// 投影几何 + CPU 射线追踪自检。
//
// 判据（默认 1mm 体素，r=50mm 球）：
//   1. 几何约定：angle=0 时源在 (0,-SAD,0)，探测器中心在 (0,SDD-SAD,0)
//   2. DDA 结果与「超采样暴力遍历」一致（< 0.5%），确认遍历算法本身正确
//   3. 中心射线线积分与解析弦长之差 ≤ 1.5 个体素（掩膜边界半体素偏差）
//   4. 球外（角落像素）线积分为 0
//   5. 相邻像素对称性：差值 ≤ 2 个体素
//   6. 90° 与 0° 的中心线积分一致（球体旋转不变）
//   7. 透射率 = exp(-μL)
//   8. 体素细化到 0.5mm 后，中心线积分与解析值之差 < 0.5%（收敛性）
// 退出码 0 表示全部通过。

#include "io/MeshLoader.h"
#include "io/VoxelGrid.h"
#include "simulation/ConeBeamView.h"
#include "simulation/CpuProjector.h"
#include "TestGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <vtkImageData.h>

namespace {

struct CheckResult
{
    bool ok = true;
    std::string name;
};

std::vector<CheckResult> g_checks;

void checkNear(const std::string& name, double actual, double expected, double relativeTolerance)
{
    const double denominator = std::max(std::fabs(expected), 1e-9);
    const bool ok = std::fabs(actual - expected) / denominator <= relativeTolerance;
    g_checks.push_back({ok, name});
    std::printf("[%s] %-40s 实际 %.4f  期望 %.4f  相对偏差 %.2f%%\n",
                ok ? "PASS" : "FAIL", name.c_str(), actual, expected, relativeTolerance * 100.0);
}

void checkAbs(const std::string& name, double actual, double expected, double absoluteTolerance)
{
    const bool ok = std::fabs(actual - expected) <= absoluteTolerance;
    g_checks.push_back({ok, name});
    std::printf("[%s] %-40s 实际 %.4f  期望 %.4f  |偏差| <= %.3f\n",
                ok ? "PASS" : "FAIL", name.c_str(), actual, expected, absoluteTolerance);
}

void checkZero(const std::string& name, double actual)
{
    checkAbs(name, actual, 0.0, 1e-6);
}

void checkExact(const std::string& name, bool condition)
{
    g_checks.push_back({condition, name});
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name.c_str());
}

float pixelAt(const precisioncbct::simulation::DrProjection& projection, int column, int row)
{
    return projection.lineIntegralMm[static_cast<size_t>(row) * projection.width + column];
}

/// 参考实现：固定步长超采样 + 最近体素查询，独立复算同一条射线的穿行长度
double bruteForceLength(const precisioncbct::io::VoxelGrid& grid,
                        const std::array<double, 3>& source,
                        const std::array<double, 3>& direction,
                        double stepMm)
{
    const auto* scalars = static_cast<const unsigned char*>(grid.image->GetScalarPointer());
    double origin[3];
    grid.image->GetOrigin(origin);

    double length = 0.0;
    // 2000mm 足以覆盖 SAD+SDD 的整个行程
    for (double t = 0.0; t < 2000.0; t += stepMm)
    {
        const double x = source[0] + direction[0] * t;
        const double y = source[1] + direction[1] * t;
        const double z = source[2] + direction[2] * t;

        const int i = static_cast<int>(std::floor((x - origin[0]) / grid.spacingMm[0]));
        const int j = static_cast<int>(std::floor((y - origin[1]) / grid.spacingMm[1]));
        const int k = static_cast<int>(std::floor((z - origin[2]) / grid.spacingMm[2]));
        if (i < 0 || j < 0 || k < 0 || i >= grid.dimensions[0] || j >= grid.dimensions[1]
            || k >= grid.dimensions[2])
        {
            continue;
        }

        const size_t flat = (static_cast<size_t>(k) * grid.dimensions[1] + j) * grid.dimensions[0] + i;
        if (scalars[flat] != 0)
        {
            length += stepMm;
        }
    }
    return length;
}

} // namespace

int main()
{
    constexpr double kRadius = 50.0;
    constexpr double kSAD = 500.0;
    constexpr double kSDD = 1000.0;
    constexpr double kPixel = 2.0;
    constexpr int kSize = 128;
    constexpr double kMu = 0.02;

    const std::filesystem::path spherePath =
        std::filesystem::temp_directory_path() / "pcsim_projection_sphere.stl";
    pcsim::test::writeBinaryStl(spherePath, pcsim::test::makeIcosphere(kRadius, 3));

    std::string error;
    const auto mesh = precisioncbct::io::loadMesh(spherePath.string(), &error);
    if (!mesh.isValid())
    {
        std::printf("FAIL 球模型读取失败: %s\n", error.c_str());
        return 1;
    }

    constexpr double kSpacing = 1.0;
    precisioncbct::io::VoxelizationOptions voxelOptions;
    voxelOptions.spacingMm = kSpacing;
    const auto grid = precisioncbct::io::voxelize(mesh.polyData, voxelOptions, &error);
    if (!grid.isValid())
    {
        std::printf("FAIL 体素化失败: %s\n", error.c_str());
        return 1;
    }

    precisioncbct::simulation::ConeBeamView view;
    view.sourceAxisDistanceMm = kSAD;
    view.sourceDetectorDistanceMm = kSDD;
    view.pixelSizeMm = kPixel;
    view.columns = kSize;
    view.rows = kSize;
    view.angleDeg = 0.0;
    checkExact("几何参数有效", view.update());

    // 1. 几何约定
    checkZero("angle=0 源 x", view.sourcePositionMm[0]);
    checkNear("angle=0 源 y", view.sourcePositionMm[1], -kSAD, 1e-9);
    checkNear("angle=0 探测器中心 y", view.detectorCenterMm[1], kSDD - kSAD, 1e-9);

    const auto centrePixel = view.pixelCenterMm(kSize / 2, kSize / 2);
    const double dx = centrePixel[0] - view.detectorCenterMm[0];
    const double dy = centrePixel[1] - view.detectorCenterMm[1];
    const double dz = centrePixel[2] - view.detectorCenterMm[2];
    const double centreOffset = std::sqrt(dx * dx + dy * dy + dz * dz);
    checkNear("中心像素到探测器中心 (mm)", centreOffset, kPixel * std::sqrt(0.5), 0.01);

    const precisioncbct::simulation::ProjectionParameters projectionParams{kMu, 1.0};
    const auto dr = precisioncbct::simulation::projectConeBeam(grid, view, projectionParams, &error);
    if (!dr.isValid())
    {
        std::printf("FAIL 投影失败: %s\n", error.c_str());
        return 1;
    }

    // 2. DDA vs 暴力超采样（同一射线、同一掩膜）
    precisioncbct::simulation::ConeBeamView singlePixel = view;
    singlePixel.columns = 1;
    singlePixel.rows = 1;
    const auto single = precisioncbct::simulation::projectConeBeam(grid, singlePixel,
                                                                   projectionParams, &error);
    if (!single.isValid())
    {
        std::printf("FAIL 单像素投影失败: %s\n", error.c_str());
        return 1;
    }
    const auto singleDirection = singlePixel.rayDirection(0, 0);
    const double reference = bruteForceLength(grid, singlePixel.sourcePositionMm, singleDirection, 0.02);
    checkNear("DDA 与暴力遍历一致", pixelAt(single, 0, 0), reference, 0.005);

    // 3~4. 解析弦长与球外区域
    const double chord = 2.0 * std::sqrt(kRadius * kRadius
                                         - std::pow(centreOffset * kSAD / kSDD, 2.0));
    checkAbs("中心射线线积分 (mm)", pixelAt(dr, kSize / 2, kSize / 2), chord, 1.5 * kSpacing);
    checkAbs("角落像素线积分为 0", pixelAt(dr, 0, 0), 0.0, 1e-6);

    // 5. 对称性（掩膜边界存在半体素级不对称，容忍 2 个体素）
    checkAbs("相邻像素线积分对称", pixelAt(dr, kSize / 2 - 1, kSize / 2),
             pixelAt(dr, kSize / 2, kSize / 2), 2.0 * kSpacing);

    // 6. 旋转不变性
    precisioncbct::simulation::ConeBeamView rotated = view;
    rotated.angleDeg = 90.0;
    checkExact("angle=90 几何有效", rotated.update());
    checkNear("angle=90 源 x", rotated.sourcePositionMm[0], kSAD, 1e-9);
    checkZero("angle=90 源 y", rotated.sourcePositionMm[1]);
    const auto drRotated = precisioncbct::simulation::projectConeBeam(grid, rotated,
                                                                     projectionParams, &error);
    if (!drRotated.isValid())
    {
        std::printf("FAIL 90 度投影失败: %s\n", error.c_str());
        return 1;
    }
    checkAbs("90 度与 0 度中心线积分一致",
             pixelAt(drRotated, kSize / 2, kSize / 2),
             pixelAt(dr, kSize / 2, kSize / 2), 1.0 * kSpacing);

    // 7. Beer-Lambert
    const size_t centreIndex = static_cast<size_t>(kSize / 2) * kSize + kSize / 2;
    const double expectedTransmission = std::exp(-kMu * dr.lineIntegralMm[centreIndex]);
    checkNear("中心透射率", dr.transmission[centreIndex], expectedTransmission, 0.001);

    // 8. 收敛性：体素减半后与解析值之差应明显缩小
    precisioncbct::io::VoxelizationOptions fineOptions;
    fineOptions.spacingMm = 0.5;
    const auto fineGrid = precisioncbct::io::voxelize(mesh.polyData, fineOptions, &error);
    if (!fineGrid.isValid())
    {
        std::printf("FAIL 0.5mm 体素化失败: %s\n", error.c_str());
        return 1;
    }
    const auto drFine = precisioncbct::simulation::projectConeBeam(fineGrid, view,
                                                                   projectionParams, &error);
    if (!drFine.isValid())
    {
        std::printf("FAIL 0.5mm 投影失败: %s\n", error.c_str());
        return 1;
    }
    checkNear("0.5mm 中心线积分 (mm)", pixelAt(drFine, kSize / 2, kSize / 2), chord, 0.005);
    checkAbs("0.5mm 相邻像素对称", pixelAt(drFine, kSize / 2 - 1, kSize / 2),
             pixelAt(drFine, kSize / 2, kSize / 2), 1.0 * fineOptions.spacingMm);

    std::printf("[info] 0.5mm 网格 %d x %d x %d，内部体素 %lld（解析期望约 %.0f）\n",
                fineGrid.dimensions[0], fineGrid.dimensions[1], fineGrid.dimensions[2],
                fineGrid.insideVoxels,
                4.0 / 3.0 * 3.14159265358979323846 * 125000.0 / 0.125);

    std::error_code ignored;
    std::filesystem::remove(spherePath, ignored);

    const auto failed = std::count_if(g_checks.begin(), g_checks.end(),
                                      [](const CheckResult& r) { return !r.ok; });
    std::printf("\n%zu 项检查，失败 %lld 项\n", g_checks.size(), static_cast<long long>(failed));
    return failed == 0 ? 0 : 1;
}
