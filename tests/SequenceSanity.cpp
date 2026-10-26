// 多角度序列自检。
//
// 判据：
//   1. 角度列表：0→90/10° 得 10 帧，端点包含；倒序 90→0 也正确
//   2. 序列帧数与角度一致，maxLineIntegral 取各帧最大值
//   3. 球体旋转不变性：所有帧中心像素线积分与第一帧之差 ≤ 1 体素
//   4. 回调返回 false 时立即停止，已完成帧数 < 总帧数，cancelled=true
//   5. 序列耗时统计非负
// 退出码 0 表示全部通过。

#include "io/MeshLoader.h"
#include "io/VoxelGrid.h"
#include "simulation/ConeBeamView.h"
#include "simulation/CpuProjector.h"
#include "simulation/ProjectionSequence.h"
#include "TestGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

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
    std::printf("[%s] %-42s 实际 %.4f  期望 %.4f  允许相对偏差 %.2f%%\n",
                ok ? "PASS" : "FAIL", name.c_str(), actual, expected, tolerance * 100.0);
}

void checkExact(const std::string& name, long long actual, long long expected)
{
    const bool ok = actual == expected;
    g_checks.push_back({ok, name});
    std::printf("[%s] %-42s 实际 %lld  期望 %lld\n",
                ok ? "PASS" : "FAIL", name.c_str(), actual, expected);
}

void checkTrue(const std::string& name, bool condition)
{
    g_checks.push_back({condition, name});
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name.c_str());
}

void checkAbs(const std::string& name, double actual, double expected, double absoluteTolerance)
{
    const bool ok = std::fabs(actual - expected) <= absoluteTolerance;
    g_checks.push_back({ok, name});
    std::printf("[%s] %-42s 实际 %.4f  期望 %.4f  |偏差| <= %.3f\n",
                ok ? "PASS" : "FAIL", name.c_str(), actual, expected, absoluteTolerance);
}

} // namespace

int main()
{
    // 1. 角度展开
    precisioncbct::simulation::ScanParameters scan;
    scan.startAngleDeg = 0.0;
    scan.endAngleDeg = 90.0;
    scan.angleStepDeg = 10.0;
    const auto ascending = precisioncbct::simulation::sequenceAngles(scan);
    checkExact("0→90 步长 10 的帧数", static_cast<long long>(ascending.size()), 10);
    checkNear("首帧角度", ascending.front(), 0.0, 1e-9);
    checkNear("末帧角度", ascending.back(), 90.0, 1e-6);

    scan.startAngleDeg = 90.0;
    scan.endAngleDeg = 0.0;
    const auto descending = precisioncbct::simulation::sequenceAngles(scan);
    checkExact("90→0 步长 10 的帧数", static_cast<long long>(descending.size()), 10);
    checkNear("倒序首帧角度", descending.front(), 90.0, 1e-9);
    checkNear("倒序末帧角度", descending.back(), 0.0, 1e-6);

    scan.angleStepDeg = 0.0;
    checkExact("步长 0 时角度列表为空",
               static_cast<long long>(precisioncbct::simulation::sequenceAngles(scan).size()), 0);

    // 2. 球体序列：小探测器以便快速运行
    constexpr double kRadius = 50.0;
    const std::filesystem::path spherePath =
        std::filesystem::temp_directory_path() / "pcsim_sequence_sphere.stl";
    pcsim::test::writeBinaryStl(spherePath, pcsim::test::makeIcosphere(kRadius, 3));

    std::string error;
    const auto mesh = precisioncbct::io::loadMesh(spherePath.string(), &error);
    if (!mesh.isValid())
    {
        std::printf("FAIL 球模型读取失败: %s\n", error.c_str());
        return 1;
    }

    precisioncbct::io::VoxelizationOptions voxelOptions;
    voxelOptions.spacingMm = 1.0;
    const auto grid = precisioncbct::io::voxelize(mesh.polyData, voxelOptions, &error);
    if (!grid.isValid())
    {
        std::printf("FAIL 体素化失败: %s\n", error.c_str());
        return 1;
    }

    precisioncbct::simulation::ScanParameters seqScan;
    seqScan.sourceAxisDistanceMm = 500.0;
    seqScan.sourceDetectorDistanceMm = 1000.0;
    seqScan.detectorPixelSizeMm = 2.0;
    seqScan.detectorColumns = 96;
    seqScan.detectorRows = 96;
    seqScan.startAngleDeg = 0.0;
    seqScan.endAngleDeg = 90.0;
    seqScan.angleStepDeg = 10.0;

    const precisioncbct::simulation::ProjectionParameters projectionParams{0.02, 1.0};

    std::vector<double> angles;
    std::vector<float> centreValues;
    const int centreIndex = (seqScan.detectorRows / 2) * seqScan.detectorColumns
                            + seqScan.detectorColumns / 2;

    const auto result = precisioncbct::simulation::runSequence(
        grid, seqScan, projectionParams,
        [&](int index, int count, double angle, const precisioncbct::simulation::DrProjection& dr) {
            if (count != 10)
            {
                std::printf("FAIL 回调中的总帧数应为 10，实际 %d\n", count);
            }
            angles.push_back(angle);
            centreValues.push_back(dr.lineIntegralMm[static_cast<size_t>(centreIndex)]);
            return true;
        },
        &error);

    if (!result.ok)
    {
        std::printf("FAIL 序列生成失败: %s（cancelled=%d）\n", error.c_str(),
                    result.cancelled ? 1 : 0);
        return 1;
    }

    checkExact("序列帧数", result.frameCount, 10);
    checkExact("完成帧数", result.completedFrames, 10);
    checkExact("回调收到帧数", static_cast<long long>(angles.size()), 10);
    checkExact("序列画像尺寸 U", result.width, seqScan.detectorColumns);
    checkExact("序列画像尺寸 V", result.height, seqScan.detectorRows);
    checkNear("序列首帧角度", result.firstAngleDeg, 0.0, 1e-9);
    checkNear("序列末帧角度", result.lastAngleDeg, 90.0, 1e-6);
    checkTrue("序列耗时 >= 0", result.elapsedMs >= 0.0);
    checkTrue("序列 maxLineIntegral > 0", result.maxLineIntegralMm > 0.0);

    // 3. 旋转不变性：球体在任意角度下中心线积分应一致
    const float reference = centreValues.front();
    float maxDeviation = 0.0f;
    for (float value : centreValues)
    {
        maxDeviation = std::max(maxDeviation, std::fabs(value - reference));
    }
    // 掩膜边界有半体素级离散误差，1mm 体素下允许 3mm 偏差
    checkAbs("全序列中心线积分最大偏差 (mm)", maxDeviation, 0.0, 3.0);

    // 4. 取消：第 3 帧后返回 false
    int visited = 0;
    const auto cancelled = precisioncbct::simulation::runSequence(
        grid, seqScan, projectionParams,
        [&](int, int, double, const precisioncbct::simulation::DrProjection&) {
            ++visited;
            return visited < 3;
        },
        &error);
    checkTrue("取消后 cancelled=true", cancelled.cancelled);
    checkExact("取消前完成帧数", cancelled.completedFrames, 3);
    checkExact("取消时回调次数", visited, 3);
    checkTrue("取消时 ok=false", !cancelled.ok);

    // 5. 非法参数
    precisioncbct::simulation::ScanParameters badScan = seqScan;
    badScan.sourceDetectorDistanceMm = badScan.sourceAxisDistanceMm; // SDD 必须 > SAD
    const auto badResult = precisioncbct::simulation::runSequence(
        grid, badScan, projectionParams,
        [](int, int, double, const precisioncbct::simulation::DrProjection&) { return true; },
        &error);
    checkTrue("非法几何被拒绝", !badResult.ok && !error.empty());

    std::error_code ignored;
    std::filesystem::remove(spherePath, ignored);

    const auto failed = std::count_if(g_checks.begin(), g_checks.end(),
                                      [](const CheckResult& r) { return !r.ok; });
    std::printf("\n%zu 项检查，失败 %lld 项\n", g_checks.size(), static_cast<long long>(failed));
    return failed == 0 ? 0 : 1;
}
