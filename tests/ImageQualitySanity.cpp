// phantom 定量验收：把「图像质量」翻译成可回归的数字。
//
// 判据（100mm 水立方 phantom，1mm 体素，SAD 500 / SDD 1000 / 像素 1mm）：
//   1. 均匀性：无噪声时中央 ROI 的透射率起伏（max-min）< 0.5%，均值与 exp(-μL) 一致；
//   2. 噪声标定：Poisson 噪声的 CV 符合 1/sqrt(N)；光子数 ×4 后 CV 减半（±25% 内）；
//   3. 低对比度可探测性：PMMA vs 水的对比度 ΔT 可测，CNR > 3，且实测与解析 1/sqrt(N) 一致；
//   4. 空间分辨率：探测器 PSF 使边缘 10–90% 过渡宽度单调变宽（σ=0 → 1 → 2 像素）；
//   5. 散射：SPR > 0 时边缘过渡变宽、最小透射率上升（散射填充暗区）。
// 退出码 0 表示全部通过。

#include "io/MeshLoader.h"
#include "io/VoxelGrid.h"
#include "simulation/ConeBeamView.h"
#include "simulation/CpuProjector.h"
#include "simulation/DetectorModel.h"
#include "simulation/SpectralProjector.h"
#include "TestGeometry.h"

#include <algorithm>
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

void report(double actual, double expected, double tolerancePercent, std::string_view name)
{
    const bool ok =
        std::fabs(actual - expected) <= tolerancePercent / 100.0 * std::max(std::fabs(expected), 1e-9);
    std::printf("[%s] %-38s 实际 %.6f  期望 %.6f  允许偏差 %.1f%%\n",
                ok ? "PASS" : "FAIL", std::string(name).c_str(), actual, expected,
                tolerancePercent);
    if (!ok)
    {
        ++g_failures;
    }
}

void reportAtMost(double actual, double limit, std::string_view name)
{
    const bool ok = actual <= limit;
    std::printf("[%s] %-38s 实际 %.6f  上限 %.6f\n", ok ? "PASS" : "FAIL",
                std::string(name).c_str(), actual, limit);
    if (!ok)
    {
        ++g_failures;
    }
}

void reportAtLeast(double actual, double limit, std::string_view name)
{
    const bool ok = actual >= limit;
    std::printf("[%s] %-38s 实际 %.6f  下限 %.6f\n", ok ? "PASS" : "FAIL",
                std::string(name).c_str(), actual, limit);
    if (!ok)
    {
        ++g_failures;
    }
}

struct Stats
{
    double mean = 0.0;
    double sigma = 0.0;
    double minValue = 0.0;
    double maxValue = 0.0;
    size_t count = 0;
};

/// 统计矩形 ROI（像素坐标，含端点）
Stats roiStats(const precisioncbct::simulation::SpectralProjection& projection,
               int column0, int row0, int column1, int row1)
{
    Stats stats;
    stats.minValue = std::numeric_limits<double>::max();
    stats.maxValue = std::numeric_limits<double>::lowest();
    double sum = 0.0;
    double sumSquares = 0.0;
    for (int row = row0; row <= row1; ++row)
    {
        for (int column = column0; column <= column1; ++column)
        {
            const double value =
                projection.transmission[static_cast<size_t>(row) * projection.width + column];
            sum += value;
            sumSquares += value * value;
            stats.minValue = std::min(stats.minValue, value);
            stats.maxValue = std::max(stats.maxValue, value);
            ++stats.count;
        }
    }
    if (stats.count == 0)
    {
        return stats;
    }
    stats.mean = sum / static_cast<double>(stats.count);
    const double variance =
        std::max(0.0, sumSquares / static_cast<double>(stats.count) - stats.mean * stats.mean);
    stats.sigma = std::sqrt(variance);
    return stats;
}

/// 沿中央行测量 10%–90% 边缘过渡宽度（像素）
///
/// phantom 在探测器上的投影是「中心暗、外围亮」，这里从左侧亮区向中心扫描，
/// 用该行最亮/最暗值定义 10% 与 90% 电平。
double edgeWidthPixels(const precisioncbct::simulation::SpectralProjection& projection)
{
    const int row = projection.height / 2;
    double brightest = std::numeric_limits<double>::lowest();
    double darkest = std::numeric_limits<double>::max();
    for (int column = 0; column < projection.width; ++column)
    {
        const double value =
            projection.transmission[static_cast<size_t>(row) * projection.width + column];
        brightest = std::max(brightest, value);
        darkest = std::min(darkest, value);
    }
    if (!(brightest > darkest))
    {
        return 0.0;
    }

    const double high = darkest + 0.9 * (brightest - darkest);
    const double low = darkest + 0.1 * (brightest - darkest);

    auto firstCrossing = [&](double level) {
        for (int column = 0; column < projection.width; ++column)
        {
            const double value =
                projection.transmission[static_cast<size_t>(row) * projection.width + column];
            if (value <= level)
            {
                return static_cast<double>(column);
            }
        }
        return static_cast<double>(projection.width);
    };

    return std::fabs(firstCrossing(low) - firstCrossing(high));
}

} // namespace

int main()
{
    using precisioncbct::simulation::DetectorModelOptions;
    using precisioncbct::simulation::SpectralProjection;
    using precisioncbct::simulation::SpectralProjectionOptions;

    constexpr double kSAD = 500.0;
    constexpr double kSDD = 1000.0;
    constexpr double kPixel = 1.0;
    constexpr int kSize = 256;
    constexpr double kCubeSide = 100.0;
    constexpr double kSpacing = 1.0;

    // ---------- phantom：100mm 水立方，中心区域路径恒为 100mm ----------
    const std::filesystem::path phantomPath =
        std::filesystem::temp_directory_path() / "pcsim_iq_cube.stl";
    pcsim::test::writeBinaryStl(phantomPath, pcsim::test::makeCube(kCubeSide));

    std::string error;
    const auto mesh = precisioncbct::io::loadMesh(phantomPath.string(), &error);
    if (!mesh.isValid())
    {
        std::printf("FAIL phantom 读取失败: %s\n", error.c_str());
        return 1;
    }

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
    if (!view.update())
    {
        std::printf("FAIL 投影几何非法\n");
        return 1;
    }

    auto makeOptions = [](bool noise, double photons, precisioncbct::simulation::Material material) {
        SpectralProjectionOptions options;
        options.spectrum.tubeVoltageKvp = 120.0;
        options.spectrum.inherentFiltrationAlMm = 2.5;
        options.material = material;
        options.incidentPhotonsPerPixel = photons;
        options.enableNoise = noise;
        options.seed = 20261026u;
        return options;
    };

    const int centre = kSize / 2;
    const int roiHalf = 16; // ±16 像素 ≈ ±8 mm（等中心处），完全落在立方体中心区域

    // ---------- 1. 均匀性与绝对衰减 ----------
    const SpectralProjection noiseless =
        precisioncbct::simulation::projectConeBeamSpectral(grid, view,
                                                          makeOptions(false, 1.0e6,
                                                                      precisioncbct::simulation::Material::Water),
                                                          &error);
    check("无噪声投影有效", noiseless.isValid());
    if (!noiseless.isValid())
    {
        std::printf("FAIL 投影失败: %s\n", error.c_str());
        return 1;
    }

    const Stats uniform = roiStats(noiseless, centre - roiHalf, centre - roiHalf,
                                   centre + roiHalf, centre + roiHalf);
    reportAtMost((uniform.maxValue - uniform.minValue) / std::max(uniform.mean, 1e-9), 0.005,
                 "均匀性：ROI 内相对起伏");

    // 解析期望：T = Σ w_E exp(-μ_w(E)·L)/Σ w_E。
    // L 用实测的中心射线线积分（体素化会带来 ±1 体素的边界误差，用实测 L 才能
    // 把「能谱加权是否正确」与「体素化边界」这两件事分开考核）。
    const double centreLineMm =
        noiseless.lineIntegralMm[static_cast<size_t>(centre) * noiseless.width + centre];
    reportAtMost(std::fabs(centreLineMm - kCubeSide), 1.5 * kSpacing, "中心射线线积分 ≈ 立方体边长");

    const auto spectrum = precisioncbct::simulation::XRaySpectrum::generate(makeOptions(false, 1e6,
                                                                                        precisioncbct::simulation::Material::Water)
                                                                              .spectrum);
    double weighted = 0.0;
    double weightSum = 0.0;
    for (const auto& bin : spectrum.bins())
    {
        const double mu =
            precisioncbct::simulation::linearAttenuationPerMm(precisioncbct::simulation::Material::Water,
                                                             bin.energyKeV);
        weighted += bin.weight * std::exp(-mu * centreLineMm);
        weightSum += bin.weight;
    }
    report(uniform.mean, weighted / weightSum, 1.0, "中心透射率与解析值一致");

    // ---------- 2. 噪声标定 ----------
    constexpr double kPhotons = 2.0e5;
    const SpectralProjection noisy =
        precisioncbct::simulation::projectConeBeamSpectral(grid, view,
                                                          makeOptions(true, kPhotons,
                                                                      precisioncbct::simulation::Material::Water),
                                                          &error);
    const SpectralProjection noisy4x =
        precisioncbct::simulation::projectConeBeamSpectral(grid, view,
                                                          makeOptions(true, kPhotons * 4.0,
                                                                      precisioncbct::simulation::Material::Water),
                                                          &error);
    check("带噪声投影有效", noisy.isValid() && noisy4x.isValid());
    if (!noisy.isValid() || !noisy4x.isValid())
    {
        return 1;
    }

    const Stats noiseStats = roiStats(noisy, centre - roiHalf, centre - roiHalf,
                                     centre + roiHalf, centre + roiHalf);
    const Stats noiseStats4x = roiStats(noisy4x, centre - roiHalf, centre - roiHalf,
                                        centre + roiHalf, centre + roiHalf);

    const double expectedSigma = std::sqrt(uniform.mean * kPhotons) / kPhotons;
    report(noiseStats.sigma, expectedSigma, 30.0, "噪声 σ 与 sqrt(N)/I0 一致");

    const double cvRatio = noiseStats.sigma / std::max(noiseStats4x.sigma, 1e-12);
    report(cvRatio, 2.0, 25.0, "光子数 ×4 时噪声减半");

    // ---------- 3. 低对比度可探测性（PMMA vs 水） ----------
    const SpectralProjection pmma =
        precisioncbct::simulation::projectConeBeamSpectral(grid, view,
                                                          makeOptions(false, kPhotons,
                                                                      precisioncbct::simulation::Material::PMMA),
                                                          &error);
    check("PMMA 投影有效", pmma.isValid());
    const Stats pmmaStats = roiStats(pmma, centre - roiHalf, centre - roiHalf,
                                     centre + roiHalf, centre + roiHalf);
    const double contrast = std::fabs(uniform.mean - pmmaStats.mean);
    const double cnr = contrast / std::max(noiseStats.sigma, 1e-12);
    std::printf("       对比度 ΔT = %.6f（相对 %.2f%%），CNR = %.2f\n", contrast,
                100.0 * contrast / std::max(uniform.mean, 1e-12), cnr);
    reportAtLeast(cnr, 3.0, "PMMA/水对比度 CNR");

    // 实测均值差（同一噪声模型）应与无噪声对比度一致
    const SpectralProjection pmmaNoisy =
        precisioncbct::simulation::projectConeBeamSpectral(grid, view,
                                                          makeOptions(true, kPhotons,
                                                                      precisioncbct::simulation::Material::PMMA),
                                                          &error);
    const Stats pmmaNoisyStats = roiStats(pmmaNoisy, centre - roiHalf, centre - roiHalf,
                                          centre + roiHalf, centre + roiHalf);
    report(std::fabs(noiseStats.mean - pmmaNoisyStats.mean), contrast, 25.0,
           "带噪声实测对比度与解析一致");

    // ---------- 4. 探测器 PSF 对分辨率的影响 ----------
    auto withPsf = [&](double sigmaPixels, double spr) {
        SpectralProjectionOptions options =
            makeOptions(false, kPhotons, precisioncbct::simulation::Material::Water);
        options.detector.detectorBlurSigmaPixels = sigmaPixels;
        options.detector.scatterToPrimaryRatio = spr;
        return precisioncbct::simulation::projectConeBeamSpectral(grid, view, options, &error);
    };

    const double edge0 = edgeWidthPixels(withPsf(0.0, 0.0));
    const double edge1 = edgeWidthPixels(withPsf(1.0, 0.0));
    const double edge2 = edgeWidthPixels(withPsf(2.0, 0.0));
    std::printf("       边缘 10–90%% 宽度：σ=0 → %.1f px，σ=1 → %.1f px，σ=2 → %.1f px\n",
                edge0, edge1, edge2);
    reportAtLeast(edge1, edge0, "PSF σ=1 使边缘变宽");
    reportAtLeast(edge2, edge1, "PSF σ=2 使边缘进一步变宽");

    // ---------- 5. 散射降低对比度 ----------
    const SpectralProjection scattered = withPsf(0.0, 0.5);
    const Stats scatteredStats = roiStats(scattered, centre - roiHalf, centre - roiHalf,
                                          centre + roiHalf, centre + roiHalf);
    std::printf("       SPR=0.5 时中心透射率 %.5f（原 %.5f），最小透射率 %.5f（原 %.5f）\n",
                scatteredStats.mean, uniform.mean, scattered.minTransmission,
                noiseless.minTransmission);
    reportAtLeast(scatteredStats.mean, uniform.mean, "散射抬高暗区（透射率上升）");
    reportAtLeast(scattered.minTransmission, noiseless.minTransmission, "散射抬高最小透射率");
    reportAtLeast(edgeWidthPixels(scattered), edge0, "散射使边缘过渡变宽");

    std::printf("\n%s：失败 %d 项\n", g_failures == 0 ? "全部通过" : "存在失败", g_failures);
    return g_failures == 0 ? 0 : 1;
}
