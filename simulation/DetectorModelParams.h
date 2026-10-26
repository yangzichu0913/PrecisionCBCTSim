#pragma once

#include <cstdint>

namespace precisioncbct::simulation {

/// 探测器与散射模型参数（Phase 3 第 3 步）。
///
/// 单独放在轻量头里，这样 `ScanParameters.h` / `SpectralProjector.h` 都能引用，
/// 而 `DetectorModel.h`（需要完整的 SpectralProjection）再包含本头，避免循环依赖。
///
/// 处理顺序（与物理过程对应）：
///   1. 散射：把初级图像做高斯模糊后按 SPR 叠加到期望光子数上
///      （散射辐射在探测器上是低空间频率的“雾”）；
///   2. 探测器 PSF：再对期望光子数做一次高斯模糊（闪烁体/光电转换的光扩散）；
///   3. Poisson 抽样：量子噪声；
///   4. 像素串扰：邻像素按比例混合（多晶硅阵列的光学/电学串扰）；
///   5. 电子学噪声：加性高斯噪声（单位：光子数）。
/// 所有参数为 0 时输出与不带探测器模型时完全一致（可作回归基准）。
struct DetectorModelOptions
{
    double scatterToPrimaryRatio = 0.0;    ///< SPR = 散射/初级（典型 0.2~0.6）
    double scatterBlurSigmaPixels = 20.0;  ///< 散射核的空间尺度（像素，低通）
    double detectorBlurSigmaPixels = 0.0;  ///< 探测器 PSF（像素）
    double crosstalkFraction = 0.0;        ///< 串扰比例（0~0.5，均匀分给 8 邻域）
    double electronicNoisePhotons = 0.0;   ///< 电子学噪声标准差（光子数）

    /// 是否会改变图像（不启用时整条链路退化为恒等变换）
    bool hasAnyEffect() const
    {
        return scatterToPrimaryRatio > 0.0 || detectorBlurSigmaPixels > 0.0
            || crosstalkFraction > 0.0 || electronicNoisePhotons > 0.0;
    }
};

/// 在「单一 μ」路径上应用探测器模型所需的上下文。
///
/// 谱路径的同类信息由 SpectralProjectionOptions 携带；两者都指向同一套
/// DetectorModelOptions，只是单一 μ 路径没有能量加权。
struct DetectorApplication
{
    DetectorModelOptions options;
    double incidentPhotonsPerPixel = 1.0e5;
    bool quantumNoise = true;
    std::uint32_t seed = 20261026u;
};

} // namespace precisioncbct::simulation
