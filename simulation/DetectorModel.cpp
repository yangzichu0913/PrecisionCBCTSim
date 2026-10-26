#include "simulation/DetectorModel.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace precisioncbct::simulation {
namespace {

std::uint32_t mixSeed(std::uint32_t seed, std::size_t index)
{
    std::uint64_t value = static_cast<std::uint64_t>(seed) * 0x9E3779B97F4A7C15ULL
                          + static_cast<std::uint64_t>(index) * 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBULL;
    value ^= value >> 31;
    return static_cast<std::uint32_t>(value);
}

/// 可分离高斯模糊（边界按最近像素延拓），输入输出均为 double。
void gaussianBlur(std::vector<double>* image, int width, int height, double sigmaPixels)
{
    if (sigmaPixels <= 0.0 || width <= 0 || height <= 0)
    {
        return;
    }

    const int radius = std::max(1, static_cast<int>(std::ceil(3.0 * sigmaPixels)));
    std::vector<double> kernel(static_cast<size_t>(2 * radius + 1));
    double sum = 0.0;
    for (int i = -radius; i <= radius; ++i)
    {
        const double value = std::exp(-0.5 * (i * i) / (sigmaPixels * sigmaPixels));
        kernel[static_cast<size_t>(i + radius)] = value;
        sum += value;
    }
    for (double& value : kernel)
    {
        value /= sum;
    }

    std::vector<double> temp(image->size(), 0.0);

    // 横向
    for (int y = 0; y < height; ++y)
    {
        const size_t row = static_cast<size_t>(y) * width;
        for (int x = 0; x < width; ++x)
        {
            double accumulator = 0.0;
            for (int k = -radius; k <= radius; ++k)
            {
                const int sx = std::clamp(x + k, 0, width - 1);
                accumulator += (*image)[row + static_cast<size_t>(sx)]
                               * kernel[static_cast<size_t>(k + radius)];
            }
            temp[row + static_cast<size_t>(x)] = accumulator;
        }
    }

    // 纵向
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            double accumulator = 0.0;
            for (int k = -radius; k <= radius; ++k)
            {
                const int sy = std::clamp(y + k, 0, height - 1);
                accumulator += temp[static_cast<size_t>(sy) * width + static_cast<size_t>(x)]
                               * kernel[static_cast<size_t>(k + radius)];
            }
            (*image)[static_cast<size_t>(y) * width + static_cast<size_t>(x)] = accumulator;
        }
    }
}

} // namespace

bool applyDetectorModel(SpectralProjection* projection,
                        const DetectorModelOptions& options,
                        double incidentPhotonsPerPixel,
                        bool quantumNoise,
                        std::uint32_t seed,
                        std::string* error)
{
    if (projection == nullptr || !projection->isValid())
    {
        if (error) { *error = "投影结果无效"; }
        return false;
    }
    if (!(incidentPhotonsPerPixel > 0.0))
    {
        if (error) { *error = "每像素入射光子数必须大于 0"; }
        return false;
    }
    if (options.crosstalkFraction < 0.0 || options.crosstalkFraction >= 0.5)
    {
        if (error) { *error = "串扰比例需在 [0, 0.5) 之间"; }
        return false;
    }
    if (options.scatterToPrimaryRatio < 0.0)
    {
        if (error) { *error = "散射比不能为负"; }
        return false;
    }

    const int width = projection->width;
    const int height = projection->height;
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);

    // 期望光子数（去噪前）
    std::vector<double> expected(pixelCount, 0.0);
    for (size_t i = 0; i < pixelCount; ++i)
    {
        expected[i] = static_cast<double>(projection->transmission[i]) * incidentPhotonsPerPixel;
    }

    // 1. 散射：模糊后的初级按 SPR 叠加
    if (options.scatterToPrimaryRatio > 0.0)
    {
        std::vector<double> scatter = expected;
        gaussianBlur(&scatter, width, height, std::max(0.0, options.scatterBlurSigmaPixels));
        for (size_t i = 0; i < pixelCount; ++i)
        {
            expected[i] += options.scatterToPrimaryRatio * scatter[i];
        }
    }

    // 2. 探测器 PSF
    gaussianBlur(&expected, width, height, options.detectorBlurSigmaPixels);

    // 3. Poisson 抽样 + 4. 串扰 + 5. 电子学噪声
    std::vector<double> counts(pixelCount, 0.0);
    for (size_t i = 0; i < pixelCount; ++i)
    {
        if (quantumNoise)
        {
            std::mt19937 generator(mixSeed(seed, i));
            std::poisson_distribution<long long> poisson(std::max(0.0, expected[i]));
            counts[i] = static_cast<double>(poisson(generator));
        }
        else
        {
            counts[i] = expected[i];
        }
    }

    if (options.crosstalkFraction > 0.0)
    {
        std::vector<double> mixed(counts.size(), 0.0);
        const double centreWeight = 1.0 - options.crosstalkFraction;
        const double neighbourWeight = options.crosstalkFraction / 8.0;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const size_t index = static_cast<size_t>(y) * width + x;
                double accumulator = counts[index] * centreWeight;
                for (int dy = -1; dy <= 1; ++dy)
                {
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        if (dx == 0 && dy == 0)
                        {
                            continue;
                        }
                        const int sy = std::clamp(y + dy, 0, height - 1);
                        const int sx = std::clamp(x + dx, 0, width - 1);
                        accumulator += counts[static_cast<size_t>(sy) * width + sx] * neighbourWeight;
                    }
                }
                mixed[index] = accumulator;
            }
        }
        counts.swap(mixed);
    }

    if (options.electronicNoisePhotons > 0.0)
    {
        for (size_t i = 0; i < pixelCount; ++i)
        {
            std::mt19937 generator(mixSeed(seed ^ 0x5A5A5A5Au, i));
            std::normal_distribution<double> gaussian(0.0, options.electronicNoisePhotons);
            counts[i] = std::max(0.0, counts[i] + gaussian(generator));
        }
    }

    // 回写
    double minTransmission = 1.0;
    for (size_t i = 0; i < pixelCount; ++i)
    {
        const double transmission = counts[i] / incidentPhotonsPerPixel;
        projection->transmission[i] = static_cast<float>(transmission);
        projection->photonCounts[i] = static_cast<std::uint32_t>(std::lround(counts[i]));
        minTransmission = std::min(minTransmission, transmission);

        const double lengthMm = projection->lineIntegralMm[i];
        projection->effectiveMuPerMm[i] =
            (lengthMm > 1e-6 && counts[i] > 0.0)
                ? static_cast<float>(-std::log(transmission) / lengthMm)
                : 0.0f;
    }
    projection->minTransmission = static_cast<float>(minTransmission);
    return true;
}

bool applyDetectorModelToDr(DrProjection* projection,
                            const DetectorModelOptions& options,
                            double incidentPhotonsPerPixel,
                            bool quantumNoise,
                            std::uint32_t seed,
                            std::string* error)
{
    if (projection == nullptr || !projection->isValid())
    {
        if (error) { *error = "投影结果无效"; }
        return false;
    }

    SpectralProjection wrapper;
    wrapper.width = projection->width;
    wrapper.height = projection->height;
    wrapper.lineIntegralMm = projection->lineIntegralMm;
    wrapper.transmission = projection->transmission;
    wrapper.photonCounts.assign(wrapper.transmission.size(), 0u);
    wrapper.effectiveMuPerMm.assign(wrapper.transmission.size(), 0.0f);

    if (!applyDetectorModel(&wrapper, options, incidentPhotonsPerPixel, quantumNoise, seed, error))
    {
        return false;
    }

    projection->transmission = wrapper.transmission;
    projection->minTransmission = wrapper.minTransmission;
    return true;
}

} // namespace precisioncbct::simulation
