#pragma once

#include <cmath>
#include <vector>

namespace precisioncbct::simulation {

/// 单个能量 bin 的光子权重
struct SpectrumBin
{
    double energyKeV = 0.0;
    double weight = 0.0; ///< 相对光子数（已含滤过衰减，且已归一化）
};

struct SpectrumParameters
{
    double tubeVoltageKvp = 100.0;       ///< 管电压 kVp
    double inherentFiltrationAlMm = 2.5; ///< 固有 + 附加铝滤过（mm Al）
    double binWidthKeV = 1.0;            ///< 能量分箱宽度
};

/// X 射线能谱模型（Phase 3 第一版）。
///
/// 模型：Kramers 定律的简化形式——连续谱光子数随能量线性下降
///   N(E) ∝ (Emax − E)
/// 再乘以铝滤过的透射率 exp(−μ_Al(E)·t)，最后归一化。
/// 这是教学/仿真级的近似：不含 W 靶特征辐射（Kα/Kβ）、不含探测器响应函数；
/// Phase 4 可替换为 SpekPy/tasmic 风格的真实谱数据。
class XRaySpectrum
{
public:
    static XRaySpectrum generate(const SpectrumParameters& parameters);

    bool isValid() const { return !m_bins.empty(); }
    const std::vector<SpectrumBin>& bins() const { return m_bins; }

    /// 光子数加权的平均能量（keV）
    double meanEnergyKeV() const { return m_meanEnergyKeV; }

    /// 能谱 bin 数
    int binCount() const { return static_cast<int>(m_bins.size()); }

    /// 谱加权透射率：T = Σ w_E·exp(−μ(E)·L) / Σ w_E
    ///
    /// 不同能量权重不同，因此长路径的等效 μ 会低于短路径（束硬化）——
    /// 这正是能谱模型相对单一 μ 的核心差异。
    /// attenuation 为可调用对象：给定能量（keV）返回线衰减系数（1/mm）。
    template <typename AttenuationFn>
    double spectralTransmission(double pathLengthMm, AttenuationFn attenuation) const
    {
        if (m_bins.empty())
        {
            return 1.0;
        }
        double weighted = 0.0;
        double weightSum = 0.0;
        for (const SpectrumBin& bin : m_bins)
        {
            weighted += bin.weight * std::exp(-attenuation(bin.energyKeV) * pathLengthMm);
            weightSum += bin.weight;
        }
        return weightSum > 0.0 ? weighted / weightSum : 1.0;
    }

private:
    std::vector<SpectrumBin> m_bins;
    double m_meanEnergyKeV = 0.0;
};

} // namespace precisioncbct::simulation
