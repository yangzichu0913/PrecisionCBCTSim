#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "simulation/DetectorModelParams.h"
#include "simulation/SpectralProjector.h"

namespace precisioncbct::simulation {

/// 探测器与散射的简化模型（Phase 3 第 3 步，纯 CPU）。
///
/// 参数含义与处理顺序见 DetectorModelOptions；本文件只负责实现。
///
/// 在谱投影结果上叠加散射 / PSF / 串扰 / 电子学噪声。
///
/// 输入需为「无噪声」的谱投影（enableNoise=false 得到的期望光子数），
/// 本函数会重写 transmission / photonCounts / effectiveMuPerMm / minTransmission。
/// quantumNoise=true 时在第 3 步做 Poisson 抽样（种子决定可复现结果）。
bool applyDetectorModel(SpectralProjection* projection,
                        const DetectorModelOptions& options,
                        double incidentPhotonsPerPixel,
                        bool quantumNoise,
                        std::uint32_t seed,
                        std::string* error);

/// 对单一 μ（无谱）路径的 DrProjection 应用同一套探测器模型。
///
/// DrProjection 只保留透射率与线积分，没有谱信息，因此散射/PSF/串扰/电子学噪声
/// 照常作用，只是不做能量加权。
bool applyDetectorModelToDr(DrProjection* projection,
                            const DetectorModelOptions& options,
                            double incidentPhotonsPerPixel,
                            bool quantumNoise,
                            std::uint32_t seed,
                            std::string* error);

} // namespace precisioncbct::simulation
