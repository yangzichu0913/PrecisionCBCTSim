#pragma once

#include <array>
#include <string>
#include <vector>

#include "simulation/MaterialTypes.h"

namespace precisioncbct::simulation {

/// 常见材料的质量衰减系数 μ/ρ（cm²/g）。
///
/// 数据点取自 NIST XCOM 的公开数值（20–150 keV，CT 常用能量段），
/// 中间能量用对对数线性插值。当前只作为 Phase 3 的第一版：
/// Phase 4 会换成完整的 μ(E) 数据文件 + 组织成分（ICRU-44）驱动的赋值。
/// 材料枚举见 simulation/MaterialTypes.h。

/// 材料的物理属性
struct MaterialProperties
{
    Material material = Material::Water;
    const char* name = "Water";
    double densityGcm3 = 1.0;               ///< 密度 g/cm³
    std::array<double, 9> massAttenuation;   ///< μ/ρ，对应 kEnergiesKeV
};

/// 表格使用的能量节点（keV），必须与 massAttenuation 一一对应
const std::array<double, 9>& attenuationEnergiesKeV();

const MaterialProperties& materialProperties(Material material);

/// 质量衰减系数 μ/ρ (cm²/g)，能量超出节点范围时按最近区间幂律外推
double massAttenuationCm2PerG(Material material, double energyKeV);

/// 线衰减系数 μ (1/mm) = μ/ρ × ρ × 0.1（cm → mm）
double linearAttenuationPerMm(Material material, double energyKeV);

/// 材料名（用于界面显示）
const char* materialName(Material material);

/// 由字符串解析材料名（不区分大小写）；失败返回 false
bool parseMaterial(const std::string& text, Material* material);

} // namespace precisioncbct::simulation
