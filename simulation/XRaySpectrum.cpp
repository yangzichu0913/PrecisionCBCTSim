#include "simulation/XRaySpectrum.h"

#include "simulation/AttenuationTable.h"

#include <algorithm>
#include <cmath>

namespace precisioncbct::simulation {
namespace {

constexpr double kMinBinWidthKeV = 0.25;
constexpr int kMaxBins = 400;

} // namespace

XRaySpectrum XRaySpectrum::generate(const SpectrumParameters& parameters)
{
    XRaySpectrum spectrum;

    if (!(parameters.tubeVoltageKvp > 0.0))
    {
        return spectrum;
    }

    const double maxEnergy = parameters.tubeVoltageKvp;
    const double binWidth = std::max(kMinBinWidthKeV, parameters.binWidthKeV);
    const double filtrationMm = std::max(0.0, parameters.inherentFiltrationAlMm);

    // 从 10 keV 起积分：更低能量几乎全被滤过吸收，纳入只会增加计算量
    constexpr double kStartEnergyKeV = 10.0;
    const int binCount = std::min(kMaxBins,
                                  static_cast<int>(std::floor((maxEnergy - kStartEnergyKeV) / binWidth)));
    if (binCount <= 0)
    {
        return spectrum;
    }

    spectrum.m_bins.reserve(static_cast<size_t>(binCount));
    double weightedEnergy = 0.0;
    for (int i = 0; i < binCount; ++i)
    {
        const double energy = kStartEnergyKeV + (i + 0.5) * binWidth;
        if (energy >= maxEnergy)
        {
            break;
        }

        // Kramers 近似：连续谱强度与 (Emax − E) 成正比
        double weight = (maxEnergy - energy) / maxEnergy;
        // 铝滤过：μ_Al(E) [1/mm] × t [mm]
        const double muAl = linearAttenuationPerMm(Material::Aluminum, energy);
        weight *= std::exp(-muAl * filtrationMm);

        if (weight <= 0.0)
        {
            continue;
        }

        spectrum.m_bins.push_back({energy, weight});
        weightedEnergy += weight * energy;
    }

    // 归一化：后续只需相对权重
    double totalWeight = 0.0;
    for (const SpectrumBin& bin : spectrum.m_bins)
    {
        totalWeight += bin.weight;
    }

    if (totalWeight > 0.0)
    {
        spectrum.m_meanEnergyKeV = weightedEnergy / totalWeight;
        for (SpectrumBin& bin : spectrum.m_bins)
        {
            bin.weight /= totalWeight;
        }
    }

    return spectrum;
}

} // namespace precisioncbct::simulation
