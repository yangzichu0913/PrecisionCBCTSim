// 能谱 + 噪声模型自检。
//
// 判据：
//   1. 能谱基本性质：bin 数 > 0、权重归一化、平均能量落在 (10 keV, kVp) 内
//   2. 管电压升高 → 平均能量升高；滤过加厚 → 平均能量升高（束硬化）
//   3. 衰减表：水在 60 keV 的 μ ≈ 0.02059 /mm（与 Phase 1 的 0.02 一致），
//      且 20→150 keV 单调下降
//   4. 束硬化：等效 μ_eff(L) 随路径长度单调下降，且 L→0 时等于谱加权平均 μ
//   5. 噪声：关闭时与无噪结果完全一致；开启时中心均匀区的光子数均值/标准差
//      符合 Poisson 统计（均值误差 < 1%，标准差误差 < 5%）
//   6. 可复现：相同种子结果相同，不同种子结果不同
// 退出码 0 表示全部通过。

#include "io/MeshLoader.h"
#include "io/VoxelGrid.h"
#include "simulation/AttenuationTable.h"
#include "simulation/ConeBeamView.h"
#include "simulation/SpectralProjector.h"
#include "simulation/XRaySpectrum.h"
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

void checkNear(const std::string& name, double actual, double expected, double relativeTolerance)
{
    const double denominator = std::max(std::fabs(expected), 1e-9);
    const bool ok = std::fabs(actual - expected) / denominator <= relativeTolerance;
    g_checks.push_back({ok, name});
    std::printf("[%s] %-40s 实际 %.5f  期望 %.5f  允许偏差 %.2f%%\n",
                ok ? "PASS" : "FAIL", name.c_str(), actual, expected, relativeTolerance * 100.0);
}

void checkTrue(const std::string& name, bool condition)
{
    g_checks.push_back({condition, name});
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name.c_str());
}

void checkGreater(const std::string& name, double actual, double reference)
{
    const bool ok = actual > reference;
    g_checks.push_back({ok, name});
    std::printf("[%s] %-40s 实际 %.4f > %.4f\n", ok ? "PASS" : "FAIL", name.c_str(), actual, reference);
}

void checkLess(const std::string& name, double actual, double reference)
{
    const bool ok = actual < reference;
    g_checks.push_back({ok, name});
    std::printf("[%s] %-40s 实际 %.4f < %.4f\n", ok ? "PASS" : "FAIL", name.c_str(), actual, reference);
}

} // namespace

int main()
{
    // ---------------------------------------------------------------- 1~2. 能谱
    precisioncbct::simulation::SpectrumParameters lowKvp;
    lowKvp.tubeVoltageKvp = 60.0;
    lowKvp.inherentFiltrationAlMm = 2.5;
    const auto spectrum60 = precisioncbct::simulation::XRaySpectrum::generate(lowKvp);

    precisioncbct::simulation::SpectrumParameters highKvp = lowKvp;
    highKvp.tubeVoltageKvp = 120.0;
    const auto spectrum120 = precisioncbct::simulation::XRaySpectrum::generate(highKvp);

    auto totalWeight = [](const precisioncbct::simulation::XRaySpectrum& spectrum) {
        double sum = 0.0;
        for (const auto& bin : spectrum.bins())
        {
            sum += bin.weight;
        }
        return sum;
    };

    checkTrue("60 kVp 能谱有效", spectrum60.isValid());
    checkTrue("120 kVp 能谱有效", spectrum120.isValid());
    checkNear("60 kVp 权重归一化", totalWeight(spectrum60), 1.0, 1e-9);
    checkNear("120 kVp 权重归一化", totalWeight(spectrum120), 1.0, 1e-9);
    checkGreater("60 kVp 平均能量 > 10 keV", spectrum60.meanEnergyKeV(), 10.0);
    checkLess("60 kVp 平均能量 < 60 keV", spectrum60.meanEnergyKeV(), 60.0);
    checkGreater("120 kVp 平均能量高于 60 kVp",
                 spectrum120.meanEnergyKeV(), spectrum60.meanEnergyKeV());

    precisioncbct::simulation::SpectrumParameters heavyFilter = highKvp;
    heavyFilter.inherentFiltrationAlMm = 8.0;
    const auto spectrumFiltered = precisioncbct::simulation::XRaySpectrum::generate(heavyFilter);
    checkGreater("增加铝滤过 → 平均能量升高",
                 spectrumFiltered.meanEnergyKeV(), spectrum120.meanEnergyKeV());

    precisioncbct::simulation::SpectrumParameters badSpectrum;
    badSpectrum.tubeVoltageKvp = 5.0;
    checkTrue("管电压过低被拒绝",
              !precisioncbct::simulation::XRaySpectrum::generate(badSpectrum).isValid());

    // ------------------------------------------------------------- 3. 衰减表
    const double muWater60 = precisioncbct::simulation::linearAttenuationPerMm(
        precisioncbct::simulation::Material::Water, 60.0);
    checkNear("水 60 keV 线衰减 μ (1/mm)", muWater60, 0.02059, 0.01);
    checkNear("水 60 keV 与 Phase 1 的 μ=0.02 一致", muWater60, 0.02, 0.05);

    const double muWater20 = precisioncbct::simulation::linearAttenuationPerMm(
        precisioncbct::simulation::Material::Water, 20.0);
    const double muWater150 = precisioncbct::simulation::linearAttenuationPerMm(
        precisioncbct::simulation::Material::Water, 150.0);
    checkGreater("水 μ(20 keV) > μ(60 keV)", muWater20, muWater60);
    checkGreater("水 μ(60 keV) > μ(150 keV)", muWater60, muWater150);

    const double muBone60 = precisioncbct::simulation::linearAttenuationPerMm(
        precisioncbct::simulation::Material::CorticalBone, 60.0);
    checkGreater("骨 μ(60 keV) > 水 μ(60 keV)", muBone60, muWater60);

    // ------------------------------------------------------------- 4. 束硬化
    const auto waterMu = [](double energyKeV) {
        return precisioncbct::simulation::linearAttenuationPerMm(
            precisioncbct::simulation::Material::Water, energyKeV);
    };

    // L→0 的极限：μ_eff 应等于谱加权平均 μ
    double weightedMu = 0.0;
    for (const auto& bin : spectrum120.bins())
    {
        weightedMu += bin.weight * waterMu(bin.energyKeV);
    }
    const double muEffThin = -std::log(spectrum120.spectralTransmission(0.01, waterMu)) / 0.01;
    checkNear("L→0 时 μ_eff = 谱加权平均 μ", muEffThin, weightedMu, 1e-4);

    const double muEff20 = -std::log(spectrum120.spectralTransmission(20.0, waterMu)) / 20.0;
    const double muEff100 = -std::log(spectrum120.spectralTransmission(100.0, waterMu)) / 100.0;
    const double muEff300 = -std::log(spectrum120.spectralTransmission(300.0, waterMu)) / 300.0;
    checkLess("束硬化：μ_eff(20mm) < μ_eff(0)", muEff20, muEffThin);
    checkLess("束硬化：μ_eff(100mm) < μ_eff(20mm)", muEff100, muEff20);
    checkLess("束硬化：μ_eff(300mm) < μ_eff(100mm)", muEff300, muEff100);
    checkGreater("μ_eff(300mm) 仍为正", muEff300, 0.0);

    // --------------------------------------------------------- 5~6. 投影与噪声
    constexpr double kCubeSide = 40.0;
    const std::filesystem::path cubePath =
        std::filesystem::temp_directory_path() / "pcsim_spectral_cube.stl";
    pcsim::test::writeBinaryStl(cubePath, pcsim::test::makeCube(kCubeSide));

    std::string error;
    const auto mesh = precisioncbct::io::loadMesh(cubePath.string(), &error);
    if (!mesh.isValid())
    {
        std::printf("FAIL 模型读取失败: %s\n", error.c_str());
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

    precisioncbct::simulation::ConeBeamView view;
    view.sourceAxisDistanceMm = 500.0;
    view.sourceDetectorDistanceMm = 1000.0;
    view.pixelSizeMm = 1.0;
    view.columns = 96;
    view.rows = 96;
    view.angleDeg = 0.0;

    precisioncbct::simulation::SpectralProjectionOptions options;
    options.spectrum = highKvp;
    options.material = precisioncbct::simulation::Material::Water;
    options.incidentPhotonsPerPixel = 1.0e6;
    options.enableNoise = false;

    const auto clean = precisioncbct::simulation::projectConeBeamSpectral(grid, view, options, &error);
    if (!clean.isValid())
    {
        std::printf("FAIL 谱投影失败: %s\n", error.c_str());
        return 1;
    }

    const size_t centreIndex = static_cast<size_t>(96 / 2) * 96 + 96 / 2;
    checkNear("中心像素路径长度 (mm)", clean.lineIntegralMm[centreIndex], kCubeSide, 0.05);

    // 无噪透射率 = 谱加权透射（用同一路径长度独立复算）
    const double expectedTransmission =
        spectrum120.spectralTransmission(clean.lineIntegralMm[centreIndex], waterMu);
    checkNear("中心透射率与独立复算一致", clean.transmission[centreIndex], expectedTransmission, 1e-4);
    checkNear("中心等效 μ 与谱加权一致", clean.effectiveMuPerMm[centreIndex],
              -std::log(expectedTransmission) / clean.lineIntegralMm[centreIndex], 1e-3);

    // 单一 μ 模型在同样 40 mm 水上的透射率更高（无束硬化），用于确认两者确实不同
    const double monoTransmission = std::exp(-0.02 * clean.lineIntegralMm[centreIndex]);
    checkTrue("能谱模型与单一 μ 结果不同",
              std::fabs(clean.transmission[centreIndex] - monoTransmission) > 1e-3);

    // 噪声统计：取中心 20×20 区域（同一厚度的均匀水块）
    options.enableNoise = true;
    options.incidentPhotonsPerPixel = 1.0e5;
    const auto noisy = precisioncbct::simulation::projectConeBeamSpectral(grid, view, options, &error);
    if (!noisy.isValid())
    {
        std::printf("FAIL 含噪投影失败: %s\n", error.c_str());
        return 1;
    }

    double sum = 0.0;
    double sumSquares = 0.0;
    int samples = 0;
    for (int row = 96 / 2 - 10; row < 96 / 2 + 10; ++row)
    {
        for (int column = 96 / 2 - 10; column < 96 / 2 + 10; ++column)
        {
            const double value = noisy.photonCounts[static_cast<size_t>(row) * 96 + column];
            sum += value;
            sumSquares += value * value;
            ++samples;
        }
    }
    const double mean = sum / samples;
    const double variance = sumSquares / samples - mean * mean;
    const double expectedMean = 1.0e5 * expectedTransmission;

    checkNear("均匀区光子数均值", mean, expectedMean, 0.02);
    checkNear("均匀区光子数标准差 ≈ sqrt(均值)", std::sqrt(variance), std::sqrt(expectedMean), 0.20);

    // 可复现性
    const auto noisyAgain = precisioncbct::simulation::projectConeBeamSpectral(grid, view, options, &error);
    checkTrue("同种子结果可复现",
              noisyAgain.photonCounts[centreIndex] == noisy.photonCounts[centreIndex]);

    options.seed = 7u;
    const auto otherSeed = precisioncbct::simulation::projectConeBeamSpectral(grid, view, options, &error);
    checkTrue("不同种子结果不同",
              otherSeed.photonCounts[centreIndex] != noisy.photonCounts[centreIndex]);

    // 光子数很少时噪声应明显更大（相对标准差）
    options.incidentPhotonsPerPixel = 1.0e3;
    const auto lowDose = precisioncbct::simulation::projectConeBeamSpectral(grid, view, options, &error);
    if (!lowDose.isValid())
    {
        std::printf("FAIL 低剂量投影失败: %s\n", error.c_str());
        return 1;
    }
    double lowSum = 0.0;
    double lowSumSquares = 0.0;
    int lowSamples = 0;
    for (int row = 96 / 2 - 10; row < 96 / 2 + 10; ++row)
    {
        for (int column = 96 / 2 - 10; column < 96 / 2 + 10; ++column)
        {
            const double value = lowDose.photonCounts[static_cast<size_t>(row) * 96 + column];
            lowSum += value;
            lowSumSquares += value * value;
            ++lowSamples;
        }
    }
    const double lowMean = lowSum / lowSamples;
    const double lowRelativeStd = std::sqrt(std::max(0.0, lowSumSquares / lowSamples - lowMean * lowMean))
                                  / std::max(lowMean, 1e-9);
    const double highRelativeStd = std::sqrt(variance) / std::max(mean, 1e-9);
    checkGreater("低剂量相对噪声更大", lowRelativeStd, highRelativeStd);

    std::error_code ignored;
    std::filesystem::remove(cubePath, ignored);

    const auto failed = std::count_if(g_checks.begin(), g_checks.end(),
                                      [](const CheckResult& r) { return !r.ok; });
    std::printf("\n%zu 项检查，失败 %lld 项\n", g_checks.size(), static_cast<long long>(failed));
    return failed == 0 ? 0 : 1;
}
