#include "simulation/SpectralProjector.h"

#include "io/MaterialLabeling.h"
#include "io/VoxelGrid.h"
#include "simulation/DetectorModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <thread>

namespace precisioncbct::simulation {
namespace {

/// 以「种子 + 像素序号」构造确定性 RNG：结果与线程数/调度无关，便于回归测试。
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

template <typename RowFn>
void parallelRows(int rowCount, RowFn&& rowFn)
{
    unsigned int threadCount = std::thread::hardware_concurrency();
    if (threadCount == 0)
    {
        threadCount = 1;
    }
    threadCount = std::min<unsigned int>(threadCount, static_cast<unsigned int>(std::max(rowCount, 1)));

    if (threadCount <= 1)
    {
        rowFn(0, rowCount);
        return;
    }

    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    const int rowsPerThread = (rowCount + static_cast<int>(threadCount) - 1)
                              / static_cast<int>(threadCount);
    for (unsigned int i = 0; i < threadCount; ++i)
    {
        const int firstRow = static_cast<int>(i) * rowsPerThread;
        const int lastRow = std::min(firstRow + rowsPerThread, rowCount);
        if (firstRow >= lastRow)
        {
            break;
        }
        workers.emplace_back(rowFn, firstRow, lastRow);
    }
    for (std::thread& worker : workers)
    {
        worker.join();
    }
}

} // namespace

SpectralProjection projectConeBeamSpectral(const io::VoxelGrid& grid,
                                           const ConeBeamView& view,
                                           const SpectralProjectionOptions& options,
                                           std::string* error)
{
    SpectralProjection projection;

    const XRaySpectrum spectrum = XRaySpectrum::generate(options.spectrum);
    if (!spectrum.isValid())
    {
        if (error) { *error = "能谱参数非法（管电压需大于 10 kV）"; }
        return projection;
    }
    if (!(options.incidentPhotonsPerPixel > 0.0))
    {
        if (error) { *error = "每像素入射光子数必须大于 0"; }
        return projection;
    }

    const bool multiMaterial = (options.labeling != nullptr && options.labeling->isValid());
    const std::vector<Material> materials = multiMaterial
                                                ? options.labeling->materials
                                                : std::vector<Material>{options.material};
    const int labelCount = static_cast<int>(materials.size());
    if (labelCount < 1 || labelCount > kMaxTrackedMaterials)
    {
        if (error) { *error = "材料层数超出支持范围（1..4）"; }
        return projection;
    }

    // 先取各材料层内的路径长度
    std::vector<float> totalLengths;
    std::vector<std::array<float, kMaxTrackedMaterials>> materialLengths;
    int width = 0;
    int height = 0;

    if (multiMaterial)
    {
        const MaterialPathLengthField field =
            computeMaterialPathLengths(grid, view, labelCount, error);
        if (!field.isValid())
        {
            return projection;
        }
        width = field.width;
        height = field.height;
        materialLengths = field.lengthsMm;

        totalLengths.assign(materialLengths.size(), 0.0f);
        for (size_t i = 0; i < materialLengths.size(); ++i)
        {
            float sum = 0.0f;
            for (int m = 0; m < labelCount; ++m)
            {
                sum += materialLengths[i][static_cast<size_t>(m)];
            }
            totalLengths[i] = sum;
        }
    }
    else
    {
        const PathLengthField field = computePathLengths(grid, view, error);
        if (!field.isValid())
        {
            return projection;
        }
        width = field.width;
        height = field.height;
        totalLengths = field.lengthsMm;
        materialLengths.assign(totalLengths.size(),
                               std::array<float, kMaxTrackedMaterials>{0.0f, 0.0f, 0.0f, 0.0f});
        for (size_t i = 0; i < totalLengths.size(); ++i)
        {
            materialLengths[i][0] = totalLengths[i];
        }
    }

    projection.width = width;
    projection.height = height;
    projection.meanEnergyKeV = spectrum.meanEnergyKeV();

    const size_t pixelCount = totalLengths.size();
    projection.lineIntegralMm = totalLengths;
    projection.transmission.assign(pixelCount, 1.0f);
    projection.photonCounts.assign(pixelCount, 0u);
    projection.effectiveMuPerMm.assign(pixelCount, 0.0f);

    const auto& bins = spectrum.bins();
    const double i0 = options.incidentPhotonsPerPixel;

    // 每个能量 bin 下各材料的 μ(E)：只在开始时算一次
    std::vector<std::array<double, kMaxTrackedMaterials>> binMu(bins.size());
    for (size_t b = 0; b < bins.size(); ++b)
    {
        binMu[b] = {0.0, 0.0, 0.0, 0.0};
        for (int m = 0; m < labelCount; ++m)
        {
            binMu[b][static_cast<size_t>(m)] = linearAttenuationPerMm(materials[static_cast<size_t>(m)],
                                                                     bins[b].energyKeV);
        }
    }

    parallelRows(height, [&](int firstRow, int lastRow) {
        for (int row = firstRow; row < lastRow; ++row)
        {
            for (int column = 0; column < width; ++column)
            {
                const size_t index = static_cast<size_t>(row) * width + column;
                const auto& lengths = materialLengths[index];

                double weighted = 0.0;
                for (size_t b = 0; b < bins.size(); ++b)
                {
                    double opticalDepth = 0.0;
                    for (int m = 0; m < labelCount; ++m)
                    {
                        opticalDepth += binMu[b][static_cast<size_t>(m)]
                                        * lengths[static_cast<size_t>(m)];
                    }
                    weighted += bins[b].weight * std::exp(-opticalDepth);
                }

                const double expectedPhotons = i0 * weighted;
                // 这里只写「期望值」；噪声（以及可选的探测器模型）在循环外统一处理，
                // 因为散射/PSF/串扰都需要整幅图像的邻域信息。
                projection.transmission[index] = static_cast<float>(weighted);
                projection.photonCounts[index] =
                    static_cast<std::uint32_t>(std::lround(expectedPhotons));
            }
        }
    });

    // 噪声：有探测器参数时整条链路（散射→PSF→Poisson→串扰→电子学噪声）由探测器模型负责；
    // 否则退化为逐像素 Poisson（与 Phase 3 第 1 步的结果完全一致）。
    if (options.detector.hasAnyEffect())
    {
        if (!applyDetectorModel(&projection, options.detector, i0, options.enableNoise,
                               options.seed, error))
        {
            return SpectralProjection{};
        }
    }
    else if (options.enableNoise)
    {
        parallelRows(height, [&](int firstRow, int lastRow) {
            for (int row = firstRow; row < lastRow; ++row)
            {
                for (int column = 0; column < width; ++column)
                {
                    const size_t index = static_cast<size_t>(row) * width + column;
                    const double expectedPhotons =
                        static_cast<double>(projection.transmission[index]) * i0;
                    std::mt19937 generator(mixSeed(options.seed, index));
                    std::poisson_distribution<std::uint32_t> poisson(std::max(0.0, expectedPhotons));
                    const double measuredPhotons = static_cast<double>(poisson(generator));

                    projection.photonCounts[index] =
                        static_cast<std::uint32_t>(std::lround(measuredPhotons));
                    projection.transmission[index] = static_cast<float>(measuredPhotons / i0);
                }
            }
        });
    }

    // 等效 μ_eff（束硬化指标）与统计量
    for (size_t index = 0; index < pixelCount; ++index)
    {
        const double lengthMm = totalLengths[index];
        const double measuredPhotons = static_cast<double>(projection.transmission[index]) * i0;
        if (lengthMm > 1e-6)
        {
            projection.effectiveMuPerMm[index] = static_cast<float>(
                -std::log(std::max(measuredPhotons, 1e-9) / i0) / lengthMm);
        }
    }

    const auto maxIt = std::max_element(projection.lineIntegralMm.begin(),
                                        projection.lineIntegralMm.end());
    const auto minTIt = std::min_element(projection.transmission.begin(),
                                         projection.transmission.end());
    projection.maxLineIntegralMm = (maxIt != projection.lineIntegralMm.end()) ? *maxIt : 0.0f;
    projection.minTransmission = (minTIt != projection.transmission.end()) ? *minTIt : 1.0f;
    return projection;
}

DrProjection toDrProjection(const SpectralProjection& spectral)
{
    DrProjection projection;
    if (!spectral.isValid())
    {
        return projection;
    }
    projection.width = spectral.width;
    projection.height = spectral.height;
    projection.lineIntegralMm = spectral.lineIntegralMm;
    projection.transmission = spectral.transmission;
    projection.maxLineIntegralMm = spectral.maxLineIntegralMm;
    projection.minTransmission = spectral.minTransmission;
    return projection;
}

} // namespace precisioncbct::simulation
