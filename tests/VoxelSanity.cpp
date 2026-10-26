// 体素化数值自检：用解析几何（球、立方体）验证「网格 → 体素」链路。
//
// 判据（不要放宽）：
//   - 球 r=50mm，1mm 体素，体积相对误差 < 5%（内接多面体本身略小于理想球）
//   - 立方体 40mm³ = 64000mm³，体积相对误差 < 8%（半体素边界的理论上界 3·spacing/L）
//   - 立方体 0.25mm 体素，误差 < 3%（分辨率提高后必须收敛）
//   - 体素维度符合 ceil(size/spacing)+1+2*padding
// 退出码 0 表示全部通过。

#include "io/MeshLoader.h"
#include "io/VoxelGrid.h"
#include "TestGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

struct CheckResult
{
    bool ok = true;
    std::string name;
};

std::vector<CheckResult> g_checks;

void checkNear(const std::string& name, double actual, double expected, double tolerance)
{
    const double denominator = std::max(std::fabs(expected), 1e-9);
    const bool ok = std::fabs(actual - expected) / denominator <= tolerance;
    g_checks.push_back({ok, name});
    std::printf("[%s] %-42s 实际 %.3f  期望 %.3f  允许偏差 %.1f%%\n",
                ok ? "PASS" : "FAIL", name.c_str(), actual, expected, tolerance * 100.0);
}

void checkExact(const std::string& name, long long actual, long long expected)
{
    const bool ok = (actual == expected);
    g_checks.push_back({ok, name});
    std::printf("[%s] %-42s 实际 %lld  期望 %lld\n",
                ok ? "PASS" : "FAIL", name.c_str(), actual, expected);
}

} // namespace

int main()
{
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path();
    const std::filesystem::path spherePath = tempDir / "pcsim_sanity_sphere.stl";
    const std::filesystem::path cubePath = tempDir / "pcsim_sanity_cube.stl";

    constexpr double kSphereRadius = 50.0; // mm
    constexpr double kCubeSide = 40.0;     // mm
    constexpr double kSpacing = 1.0;       // mm

    pcsim::test::writeBinaryStl(spherePath, pcsim::test::makeIcosphere(kSphereRadius, 3));
    pcsim::test::writeBinaryStl(cubePath, pcsim::test::makeCube(kCubeSide));

    precisioncbct::io::VoxelizationOptions options;
    options.spacingMm = kSpacing;
    options.paddingVoxels = 2;

    std::string error;

    const auto sphereMesh = precisioncbct::io::loadMesh(spherePath.string(), &error);
    checkExact("球网格三角面片数", sphereMesh.triangleCount, 1280);
    if (!sphereMesh.isValid())
    {
        std::printf("FAIL 球模型读取失败: %s\n", error.c_str());
        return 1;
    }

    const auto sphereGrid = precisioncbct::io::voxelize(sphereMesh.polyData, options, &error);
    if (!sphereGrid.isValid())
    {
        std::printf("FAIL 球体素化失败: %s\n", error.c_str());
        return 1;
    }
    const double expectedSphereVolume =
        4.0 / 3.0 * kPi * kSphereRadius * kSphereRadius * kSphereRadius;
    checkNear("球体积 (mm³)", sphereGrid.insideVolumeMm3(), expectedSphereVolume, 0.05);
    const int expectedDim =
        static_cast<int>(std::ceil(2.0 * kSphereRadius / kSpacing)) + 1 + 2 * options.paddingVoxels;
    checkExact("球体素 X 维度", sphereGrid.dimensions[0], expectedDim);
    checkExact("球体素 Y 维度", sphereGrid.dimensions[1], expectedDim);
    checkExact("球体素 Z 维度", sphereGrid.dimensions[2], expectedDim);

    const auto cubeMesh = precisioncbct::io::loadMesh(cubePath.string(), &error);
    checkExact("立方体三角面片数", cubeMesh.triangleCount, 12);
    if (!cubeMesh.isValid())
    {
        std::printf("FAIL 立方体模型读取失败: %s\n", error.c_str());
        return 1;
    }

    const auto cubeGrid = precisioncbct::io::voxelize(cubeMesh.polyData, options, &error);
    if (!cubeGrid.isValid())
    {
        std::printf("FAIL 立方体体素化失败: %s\n", error.c_str());
        return 1;
    }
    checkNear("立方体体积 1mm 体素 (mm³)", cubeGrid.insideVolumeMm3(),
              kCubeSide * kCubeSide * kCubeSide, 0.08);

    // 收敛性：体素变细后误差必须显著下降（spacing=0.25mm 时上界约 1.9%）
    precisioncbct::io::VoxelizationOptions fineOptions;
    fineOptions.spacingMm = 0.25;
    fineOptions.paddingVoxels = 2;
    const auto fineGrid = precisioncbct::io::voxelize(cubeMesh.polyData, fineOptions, &error);
    if (!fineGrid.isValid())
    {
        std::printf("FAIL 立方体细分体素化失败: %s\n", error.c_str());
        return 1;
    }
    checkNear("立方体体积 0.25mm 体素 (mm³)", fineGrid.insideVolumeMm3(),
              kCubeSide * kCubeSide * kCubeSide, 0.03);

    std::error_code ignored;
    std::filesystem::remove(spherePath, ignored);
    std::filesystem::remove(cubePath, ignored);

    const auto failed = std::count_if(g_checks.begin(), g_checks.end(),
                                      [](const CheckResult& r) { return !r.ok; });
    std::printf("\n%zu 项检查，失败 %lld 项\n", g_checks.size(), static_cast<long long>(failed));
    return failed == 0 ? 0 : 1;
}
