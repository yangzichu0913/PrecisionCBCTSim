#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "simulation/AttenuationTable.h"
#include "simulation/CpuProjector.h"
#include "simulation/DetectorModelParams.h"
#include "simulation/MaterialTypes.h"
#include "simulation/XRaySpectrum.h"

namespace precisioncbct::io {
struct VoxelGrid;
struct MaterialLabeling;
}

namespace precisioncbct::simulation {

struct SpectralProjectionOptions
{
    SpectrumParameters spectrum;
    /// 单材料模式使用的材料（labeling 为空时生效）
    Material material = Material::Water;
    /// 多材料模式：由 io::labelMaterials 生成的标签；非空时按标签分别累计路径
    const io::MaterialLabeling* labeling = nullptr;
    double incidentPhotonsPerPixel = 1.0e5; ///< 每像素入射光子数（决定噪声水平）
    bool enableNoise = true;                ///< Poisson 量子噪声
    std::uint32_t seed = 20261026u;         ///< 噪声随机种子（保证可复现）
    /// 探测器与散射模型；非零时由它统一做「散射 → PSF → Poisson → 串扰 → 电子学噪声」，
    /// 此时 enableNoise 只表示是否在这一链路里做 Poisson 抽样（避免重复加噪）。
    DetectorModelOptions detector;
};

/// 谱加权 + 噪声的 DR 投影结果。
///
/// transmission 为「探测到的光子数 / 入射光子数」；同时保留：
/// - photonCounts：整数光子数（噪声开启时为 Poisson 抽样结果）
/// - effectiveMuPerMm：等效线衰减系数 μ_eff = −ln(T)/L（用于观察束硬化）
struct SpectralProjection
{
    int width = 0;
    int height = 0;
    std::vector<float> lineIntegralMm;   ///< 物体内总路径长度
    std::vector<float> transmission;     ///< I/I0（含噪声时为实测值）
    std::vector<std::uint32_t> photonCounts;
    std::vector<float> effectiveMuPerMm; ///< 路径为 0 时置 0
    float maxLineIntegralMm = 0.0f;
    float minTransmission = 1.0f;
    double meanEnergyKeV = 0.0;

    bool isValid() const
    {
        return width > 0 && height > 0
            && transmission.size() == static_cast<size_t>(width) * static_cast<size_t>(height);
    }
};

/// 用能谱模型做锥束投影（CPU）。
///
/// 与单 μ 版本的区别：
/// 1. 每条射线按多能 bin 加权，`T = Σ w_E exp(−Σ_m μ_m(E)·L_m) / Σ w_E`；
///    因此随路径增长会出现束硬化（μ_eff 下降），多材料时也是线束硬化伪影的成因；
/// 2. 可选 Poisson 量子噪声：先算期望光子数，再做泊松抽样；
/// 3. options.labeling 非空时改为多材料：grid 必须是「标签体数据」
///    （把 VoxelGrid.image 换成 labeling->labels，spacing/origin 保持一致）。
SpectralProjection projectConeBeamSpectral(const io::VoxelGrid& grid,
                                           const ConeBeamView& view,
                                           const SpectralProjectionOptions& options,
                                           std::string* error);

/// 把谱加权结果转成通用的 DrProjection（显示/导出复用同一套代码路径）
DrProjection toDrProjection(const SpectralProjection& spectral);

} // namespace precisioncbct::simulation
