// 探测器模型（散射 / PSF / 串扰 / 电子学噪声）自检。
//
// 全部在合成图像上验证，不依赖几何与体素化，便于定位问题：
//   1. 全零参数：输出与输入完全一致（回归基准）
//   2. 散射：均值按 (1 + SPR) 放大（均匀场），且 SPR 越大均值越大
//   3. PSF：脉冲被展宽（中心下降、邻域上升），总光子数守恒（< 0.5%）
//   4. 串扰：总光子数守恒（< 0.5%），中心像素被削弱
//   5. 电子学噪声：均值保持不变（< 1%），标准差 ≈ 设定值（±20%）
//   6. 组合模型：量子噪声存在时相邻像素不再相等
// 退出码 0 表示全部通过。

#include "simulation/DetectorModel.h"
#include "simulation/SpectralProjector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
    std::printf("[%s] %-42s 实际 %.5f  期望 %.5f  允许偏差 %.1f%%\n",
                ok ? "PASS" : "FAIL", name.c_str(), actual, expected, tolerance * 100.0);
}

void checkTrue(const std::string& name, bool condition)
{
    g_checks.push_back({condition, name});
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name.c_str());
}

/// 生成一个 width×height、transmission 全为 value 的合成投影
precisioncbct::simulation::SpectralProjection makeFlat(int width, int height, double value)
{
    precisioncbct::simulation::SpectralProjection projection;
    projection.width = width;
    projection.height = height;
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    projection.lineIntegralMm.assign(count, 100.0f);
    projection.transmission.assign(count, static_cast<float>(value));
    projection.photonCounts.assign(count, 0u);
    projection.effectiveMuPerMm.assign(count, 0.0f);
    projection.minTransmission = static_cast<float>(value);
    return projection;
}

double meanTransmission(const precisioncbct::simulation::SpectralProjection& projection)
{
    double sum = 0.0;
    for (float value : projection.transmission)
    {
        sum += value;
    }
    return sum / static_cast<double>(projection.transmission.size());
}

double totalPhotons(const precisioncbct::simulation::SpectralProjection& projection)
{
    double sum = 0.0;
    for (std::uint32_t value : projection.photonCounts)
    {
        sum += static_cast<double>(value);
    }
    return sum;
}

} // namespace

int main()
{
    constexpr int kWidth = 64;
    constexpr int kHeight = 64;
    constexpr double kI0 = 1.0e5;
    constexpr double kT = 0.4;

    std::string error;

    // ---------------------------------------------- 1. 全零参数 = 恒等
    {
        auto projection = makeFlat(kWidth, kHeight, kT);
        const auto reference = projection;
        precisioncbct::simulation::DetectorModelOptions options; // 全 0
        checkTrue("全零参数应用成功",
                  precisioncbct::simulation::applyDetectorModel(&projection, options, kI0, false, 1u,
                                                               &error));
        bool identical = true;
        for (size_t i = 0; i < projection.transmission.size(); ++i)
        {
            if (std::fabs(projection.transmission[i] - reference.transmission[i]) > 1e-6f)
            {
                identical = false;
                break;
            }
        }
        checkTrue("全零参数输出与输入一致（回归）", identical);
    }

    // ---------------------------------------------- 2. 散射
    {
        auto projection = makeFlat(kWidth, kHeight, kT);
        precisioncbct::simulation::DetectorModelOptions options;
        options.scatterToPrimaryRatio = 0.5;
        options.scatterBlurSigmaPixels = 3.0;
        checkTrue("散射模型应用成功",
                  precisioncbct::simulation::applyDetectorModel(&projection, options, kI0, false, 1u,
                                                               &error));
        // 均匀场经过归一化高斯模糊后仍是自身，因此 T' = (1 + SPR)·T
        checkNear("散射使均值放大 (1+SPR) 倍", meanTransmission(projection), kT * 1.5, 0.01);

        auto stronger = makeFlat(kWidth, kHeight, kT);
        options.scatterToPrimaryRatio = 1.0;
        precisioncbct::simulation::applyDetectorModel(&stronger, options, kI0, false, 1u, &error);
        checkTrue("SPR 越大均值越高", meanTransmission(stronger) > meanTransmission(projection));
    }

    // ---------------------------------------------- 3. 探测器 PSF
    {
        auto projection = makeFlat(kWidth, kHeight, 0.0);
        // 中央单像素脉冲
        const int cx = kWidth / 2;
        const int cy = kHeight / 2;
        projection.transmission[static_cast<size_t>(cy) * kWidth + cx] = static_cast<float>(kT);
        // 期望光子总数 = 该像素的透射率 × I0（photonCounts 此时还未填充）
        const double beforeTotal = kT * kI0;

        precisioncbct::simulation::DetectorModelOptions options;
        options.detectorBlurSigmaPixels = 2.0;
        checkTrue("PSF 应用成功",
                  precisioncbct::simulation::applyDetectorModel(&projection, options, kI0, false, 1u,
                                                               &error));

        const double centre = projection.transmission[static_cast<size_t>(cy) * kWidth + cx];
        const double neighbour = projection.transmission[static_cast<size_t>(cy) * kWidth + cx + 2];
        checkTrue("PSF 使中心像素被削弱", centre < kT);
        checkTrue("PSF 使邻域像素抬升", neighbour > 0.0);
        checkNear("PSF 保持总光子数", totalPhotons(projection), beforeTotal, 0.005);
    }

    // ---------------------------------------------- 4. 串扰
    {
        auto projection = makeFlat(kWidth, kHeight, 0.0);
        const int cx = kWidth / 2;
        const int cy = kHeight / 2;
        projection.transmission[static_cast<size_t>(cy) * kWidth + cx] = static_cast<float>(kT);
        // 期望光子总数 = 该像素的透射率 × I0（photonCounts 此时还未填充）
        const double beforeTotal = kT * kI0;

        precisioncbct::simulation::DetectorModelOptions options;
        options.crosstalkFraction = 0.3;
        checkTrue("串扰模型应用成功",
                  precisioncbct::simulation::applyDetectorModel(&projection, options, kI0, false, 1u,
                                                               &error));

        const double centre = projection.transmission[static_cast<size_t>(cy) * kWidth + cx];
        checkTrue("串扰削弱中心像素", centre < kT);
        checkNear("串扰保持总光子数", totalPhotons(projection), beforeTotal, 0.005);
    }

    // ---------------------------------------------- 5. 电子学噪声
    {
        auto projection = makeFlat(kWidth, kHeight, kT);
        precisioncbct::simulation::DetectorModelOptions options;
        options.electronicNoisePhotons = 500.0;
        checkTrue("电子学噪声应用成功",
                  precisioncbct::simulation::applyDetectorModel(&projection, options, kI0, false, 7u,
                                                               &error));

        const double mean = meanTransmission(projection);
        checkNear("电子学噪声不改变均值", mean, kT, 0.01);

        double sumSquares = 0.0;
        for (float value : projection.transmission)
        {
            const double delta = (value - mean) * kI0;
            sumSquares += delta * delta;
        }
        const double stdPhotons = std::sqrt(sumSquares / projection.transmission.size());
        checkNear("电子学噪声标准差（光子）", stdPhotons, 500.0, 0.20);
    }

    // ---------------------------------------------- 6. 组合 + 量子噪声
    {
        auto projection = makeFlat(kWidth, kHeight, kT);
        precisioncbct::simulation::DetectorModelOptions options;
        options.scatterToPrimaryRatio = 0.4;
        options.scatterBlurSigmaPixels = 4.0;
        options.detectorBlurSigmaPixels = 1.5;
        options.crosstalkFraction = 0.1;
        options.electronicNoisePhotons = 20.0;
        checkTrue("组合模型应用成功",
                  precisioncbct::simulation::applyDetectorModel(&projection, options, kI0, true, 99u,
                                                               &error));

        bool hasVariation = false;
        for (size_t i = 1; i < projection.photonCounts.size(); ++i)
        {
            if (projection.photonCounts[i] != projection.photonCounts[0])
            {
                hasVariation = true;
                break;
            }
        }
        checkTrue("量子噪声使像素值不再均匀", hasVariation);
        checkTrue("等效 μ 已重算", projection.effectiveMuPerMm[0] > 0.0f);
    }

    // ---------------------------------------------- 参数校验
    {
        auto projection = makeFlat(8, 8, kT);
        precisioncbct::simulation::DetectorModelOptions options;
        options.crosstalkFraction = 0.7;
        checkTrue("非法串扰比例被拒绝",
                  !precisioncbct::simulation::applyDetectorModel(&projection, options, kI0, false, 1u,
                                                                 &error) && !error.empty());
    }

    const auto failed = std::count_if(g_checks.begin(), g_checks.end(),
                                      [](const CheckResult& r) { return !r.ok; });
    std::printf("\n%zu 项检查，失败 %lld 项\n", g_checks.size(), static_cast<long long>(failed));
    return failed == 0 ? 0 : 1;
}
