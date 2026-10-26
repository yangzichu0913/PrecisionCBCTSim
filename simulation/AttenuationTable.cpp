#include "simulation/AttenuationTable.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace precisioncbct::simulation {
namespace {

// 能量节点（keV）
const std::array<double, 9> kEnergies = {20.0, 30.0, 40.0, 50.0, 60.0, 80.0, 100.0, 120.0, 150.0};

// 质量衰减系数 μ/ρ（cm²/g），按 kEnergies 排列（NIST XCOM 公开数据，保留 4 位有效数字）。
// 水的 60 keV 值 0.2059 cm²/g → μ = 0.02059 /mm，与 Phase 1 使用的 0.02 /mm 一致。
const std::array<MaterialProperties, 5> kMaterials = {{
    {Material::Water, "Water", 1.000,
     {0.8096, 0.3756, 0.2683, 0.2269, 0.2059, 0.1837, 0.1707, 0.1613, 0.1505}},
    {Material::CorticalBone, "CorticalBone", 1.920,
     {5.5120, 1.8740, 1.0650, 0.7154, 0.5312, 0.3501, 0.2727, 0.2371, 0.2067}},
    {Material::PMMA, "PMMA", 1.190,
     {1.0710, 0.4700, 0.3300, 0.2780, 0.2510, 0.2210, 0.2030, 0.1900, 0.1750}},
    {Material::Air, "Air", 0.001205,
     {0.7786, 0.3538, 0.2485, 0.2080, 0.1875, 0.1662, 0.1541, 0.1454, 0.1356}},
    {Material::Aluminum, "Aluminum", 2.699,
     {3.4420, 1.1330, 0.5822, 0.3681, 0.2782, 0.2023, 0.1704, 0.1537, 0.1379}},
}};

std::string toLower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

const std::array<double, 9>& attenuationEnergiesKeV()
{
    return kEnergies;
}

const MaterialProperties& materialProperties(Material material)
{
    for (const MaterialProperties& properties : kMaterials)
    {
        if (properties.material == material)
        {
            return properties;
        }
    }
    return kMaterials.front();
}

double massAttenuationCm2PerG(Material material, double energyKeV)
{
    const MaterialProperties& properties = materialProperties(material);
    const auto& values = properties.massAttenuation;

    if (!(energyKeV > 0.0))
    {
        return values.front();
    }

    // 节点范围内：对对数线性插值（μ/ρ 在双对数坐标下近似直线）
    if (energyKeV <= kEnergies.front())
    {
        // 低于最低节点：用首两个节点的幂律指数外推
        const double exponent = std::log(values[0] / values[1]) / std::log(kEnergies[1] / kEnergies[0]);
        return values[0] * std::pow(energyKeV / kEnergies[0], exponent);
    }
    if (energyKeV >= kEnergies.back())
    {
        const size_t last = kEnergies.size() - 1;
        const double exponent = std::log(values[last] / values[last - 1])
                                / std::log(kEnergies[last] / kEnergies[last - 1]);
        return values[last] * std::pow(energyKeV / kEnergies[last], exponent);
    }

    for (size_t i = 0; i + 1 < kEnergies.size(); ++i)
    {
        if (energyKeV <= kEnergies[i + 1])
        {
            const double t = std::log(energyKeV / kEnergies[i])
                             / std::log(kEnergies[i + 1] / kEnergies[i]);
            const double logValue = std::log(values[i]) + t * (std::log(values[i + 1]) - std::log(values[i]));
            return std::exp(logValue);
        }
    }
    return values.back();
}

double linearAttenuationPerMm(Material material, double energyKeV)
{
    const MaterialProperties& properties = materialProperties(material);
    // μ/ρ [cm²/g] × ρ [g/cm³] = μ [1/cm]；再 × 0.1 得到 1/mm
    return massAttenuationCm2PerG(material, energyKeV) * properties.densityGcm3 * 0.1;
}

const char* materialName(Material material)
{
    return materialProperties(material).name;
}

bool parseMaterial(const std::string& text, Material* material)
{
    const std::string lowered = toLower(text);
    for (const MaterialProperties& properties : kMaterials)
    {
        if (toLower(properties.name) == lowered)
        {
            if (material) { *material = properties.material; }
            return true;
        }
    }
    return false;
}

} // namespace precisioncbct::simulation
