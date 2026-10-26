// FDK 锥束重建的自检。
//
// 用「球体 phantom 的解析真实值」验证重建的量纲与正确性：
//   1. 球心 μ 应等于 0.02 /mm（允许 8%：离散 ramp 滤波 + 有限角度采样）
//   2. 球外（角落）μ 应接近 0（≤ 球心的 15%）
//   3. 量纲必须与探测器像素尺寸无关（1.0mm 与 2.0mm 都应重建出真实 μ）
//   4. 线衰减系数是线性的：μ 翻倍 → 重建值翻倍（误差 ≤ 2%）
//   5. 均匀球内部的一致性：半径 0.5R 内的标准差 ≤ 均值的 8%
//   6. 角度不足时仍能出结果、不产生 NaN；Hann 窗保持量级
//   7. 非法输入（体素太少、非法几何、空序列、尺寸不匹配）返回无效结果
// 第 3 条是本项目踩过的坑：ramp 归一化写错时重建值会随像素尺寸线性漂移
// （0.4mm 时只有真实值的 1/5），只测一种像素尺寸根本发现不了。
// 退出码 0 表示全部通过。

#include "io/MeshLoader.h"
#include "io/VoxelGrid.h"
#include "simulation/ConeBeamView.h"
#include "simulation/CpuProjector.h"
#include "simulation/FdkReconstructor.h"
#include "TestGeometry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

int g_failures = 0;

void check(std::string_view name, bool condition)
{
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", std::string(name).c_str());
    if (!condition)
    {
        ++g_failures;
    }
}

void reportNear(std::string_view name, double actual, double expected, double tolerancePercent)
{
    const bool ok = std::fabs(actual - expected)
                    <= tolerancePercent / 100.0 * std::max(std::fabs(expected), 1e-9);
    std::printf("[%s] %-40s 实际 %.6f  期望 %.6f  允许偏差 %.1f%%\n", ok ? "PASS" : "FAIL",
                std::string(name).c_str(), actual, expected, tolerancePercent);
    if (!ok)
    {
        ++g_failures;
    }
}

} // namespace

int main()
{
    using precisioncbct::simulation::ConeBeamView;
    using precisioncbct::simulation::FdkOptions;
    using precisioncbct::simulation::FdkReconstructor;
    using precisioncbct::simulation::ProjectionParameters;

    constexpr double kRadiusMm = 50.0;
    constexpr double kMuPerMm = 0.02;
    constexpr double kSAD = 500.0;
    constexpr double kSDD = 1000.0;
    constexpr double kPixel = 2.0;
    constexpr int kDetector = 160;

    // ---------- phantom：r=50mm 球 ----------
    const std::filesystem::path spherePath =
        std::filesystem::temp_directory_path() / "pcsim_fdk_sphere.stl";
    pcsim::test::writeBinaryStl(spherePath, pcsim::test::makeIcosphere(kRadiusMm, 3));

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

    ConeBeamView view;
    view.sourceAxisDistanceMm = kSAD;
    view.sourceDetectorDistanceMm = kSDD;
    view.pixelSizeMm = kPixel;
    view.columns = kDetector;
    view.rows = kDetector;
    if (!view.update())
    {
        std::printf("FAIL 投影几何非法\n");
        return 1;
    }

    ProjectionParameters projectionParams;
    projectionParams.attenuationPerMm = kMuPerMm;

    // 跑一圈投影并即时喂给重建器（与主程序的流式用法一致）
    const auto reconstructAt = [&](double muPerMm, int angleCount, bool hann, double pixelSizeMm,
                                   double* elapsedMs) {
        ProjectionParameters params = projectionParams;
        params.attenuationPerMm = muPerMm;

        FdkOptions options;
        options.volumeSize = 64;
        options.voxelSizeMm = 2.0;
        options.hannWindow = hann;

        ConeBeamView geometry = view;
        geometry.pixelSizeMm = pixelSizeMm;
        FdkReconstructor reconstructor(options, geometry);

        const auto started = std::chrono::steady_clock::now();
        for (int index = 0; index < angleCount; ++index)
        {
            ConeBeamView frame = geometry;
            frame.angleDeg = 360.0 * index / angleCount;
            if (!frame.update())
            {
                continue;
            }
            const auto projection =
                precisioncbct::simulation::projectConeBeam(grid, frame, params, &error);
            if (!projection.isValid())
            {
                continue;
            }
            reconstructor.addProjection(frame.angleDeg, projection);
        }
        const auto finished = std::chrono::steady_clock::now();
        if (elapsedMs != nullptr)
        {
            *elapsedMs = std::chrono::duration<double, std::milli>(finished - started).count();
        }
        return reconstructor.volume();
    };

    const auto reconstruct = [&](double muPerMm, int angleCount, bool hann, double* elapsedMs) {
        return reconstructAt(muPerMm, angleCount, hann, kPixel, elapsedMs);
    };

    // ---------- 1~2. 标准重建 ----------
    double elapsedMs = 0.0;
    const auto volume = reconstruct(kMuPerMm, 120, false, &elapsedMs);
    check("重建结果有效", volume.isValid());
    if (!volume.isValid())
    {
        std::printf("FAIL 重建失败\n");
        return 1;
    }
    std::printf("       体素数 %d³ @ %.2f mm，120 帧，耗时 %.0f ms（含投影计算）\n",
                volume.dimensions[0], volume.spacingMm[0], elapsedMs);

    const int centre = volume.dimensions[0] / 2;
    const double centreValue = volume.at(centre, centre, centre);
    reportNear("球心 μ（真实 0.02 /mm）", centreValue, kMuPerMm, 8.0);

    const int corner = 5;
    const double cornerValue = volume.at(corner, corner, corner);
    std::printf("       球外角落 μ = %.6f（球心的 %.1f%%）\n", cornerValue,
                100.0 * cornerValue / std::max(centreValue, 1e-12));
    check("球外衰减接近 0", std::fabs(cornerValue) <= 0.15 * centreValue);

    // ---------- 3. 量纲与像素尺寸无关 ----------
    {
        const auto finePixel = reconstructAt(kMuPerMm, 60, false, 1.0, nullptr);
        check("1.0mm 像素的重建有效", finePixel.isValid());
        if (finePixel.isValid())
        {
            reportNear("1.0mm 像素的球心 μ", finePixel.at(centre, centre, centre), kMuPerMm, 8.0);
            // 两者的差异只来自离散化（2.0mm 像素明显更粗）。这里只做「粗差」护栏：
            // 真正要紧的是两者都贴近真实 μ；之前 ramp 归一化写错时偏差是 5 倍，
            // 用 12% 的互相一致性足以抓住这类量纲错误，又不会误报正常的离散化差异。
            reportNear("与 2.0mm 像素结果一致", finePixel.at(centre, centre, centre), centreValue,
                       12.0);
        }
    }

    // ---------- 5. 均匀性 ----------
    {
        const double roiRadius = 0.5 * kRadiusMm;
        double sum = 0.0;
        double sumSquares = 0.0;
        size_t count = 0;
        for (int k = 0; k < volume.dimensions[2]; ++k)
        {
            for (int j = 0; j < volume.dimensions[1]; ++j)
            {
                for (int i = 0; i < volume.dimensions[0]; ++i)
                {
                    const auto xyz = volume.voxelCenterMm(i, j, k);
                    const double distance =
                        std::sqrt(xyz[0] * xyz[0] + xyz[1] * xyz[1] + xyz[2] * xyz[2]);
                    if (distance > roiRadius)
                    {
                        continue;
                    }
                    const double value = volume.at(i, j, k);
                    sum += value;
                    sumSquares += value * value;
                    ++count;
                }
            }
        }
        const double mean = sum / static_cast<double>(count);
        const double variance =
            std::max(0.0, sumSquares / static_cast<double>(count) - mean * mean);
        const double sigma = std::sqrt(variance);
        std::printf("       0.5R 内：均值 %.6f，标准差 %.6f（%.1f%%）\n", mean, sigma,
                    100.0 * sigma / std::max(mean, 1e-12));
        reportNear("球内均值", mean, kMuPerMm, 8.0);
        check("球内均匀性（σ/均值 ≤ 8%）", sigma / std::max(mean, 1e-12) <= 0.08);
    }

    // ---------- 4. 线性：μ 翻倍 ----------
    {
        // 线性检查必须与基线用同样的帧数，否则角度采样差异会被误判成非线性
        const auto doubled = reconstruct(2.0 * kMuPerMm, 120, false, nullptr);
        check("加倍 μ 的重建有效", doubled.isValid());
        if (doubled.isValid())
        {
            reportNear("μ 翻倍后重建值翻倍", doubled.at(centre, centre, centre),
                       2.0 * centreValue, 2.0);
        }
    }

    // ---------- 6. 角度不足 / Hann 窗 ----------
    {
        const auto partial = reconstruct(kMuPerMm, 30, false, nullptr);
        check("角度不足（30/120 帧）仍能出结果", partial.isValid());
        if (partial.isValid())
        {
            size_t finite = 0;
            for (float value : partial.attenuationPerMm)
            {
                if (std::isfinite(value))
                {
                    ++finite;
                }
            }
            check("角度不足时没有 NaN/Inf", finite == partial.attenuationPerMm.size());
            std::printf("       角度不足时球心 μ = %.6f（会偏离真实值，属预期）\n",
                        partial.at(centre, centre, centre));
        }

        const auto windowed = reconstruct(kMuPerMm, 60, true, nullptr);
        check("Hann 窗重建有效", windowed.isValid());
        if (windowed.isValid())
        {
            reportNear("Hann 窗球心 μ 仍接近真实值", windowed.at(centre, centre, centre),
                       kMuPerMm, 15.0);
        }
    }

    // ---------- 7. 非法输入 ----------
    {
        FdkOptions badOptions;
        badOptions.volumeSize = 4; // 太小
        const FdkReconstructor tooSmall(badOptions, view);
        check("体素数过小被拒绝", !tooSmall.isValid());

        ConeBeamView badGeometry = view;
        badGeometry.sourceDetectorDistanceMm = badGeometry.sourceAxisDistanceMm; // SDD == SAD
        FdkOptions options;
        options.volumeSize = 48;
        const FdkReconstructor badView(options, badGeometry);
        check("非法几何被拒绝", !badView.isValid());

        FdkReconstructor empty(options, view);
        check("空重建器返回无效结果", !empty.volume().isValid());

        FdkReconstructor mismatched(options, view);
        precisioncbct::simulation::DrProjection wrongSize;
        wrongSize.width = 8;
        wrongSize.height = 8;
        wrongSize.lineIntegralMm.assign(64, 0.0f);
        wrongSize.transmission.assign(64, 1.0f);
        check("尺寸不匹配的帧被忽略", !mismatched.addProjection(0.0, wrongSize));
        check("忽略后仍无有效体积", !mismatched.volume().isValid());
    }

    std::printf("\n%s：失败 %d 项\n", g_failures == 0 ? "全部通过" : "存在失败", g_failures);
    return g_failures == 0 ? 0 : 1;
}
