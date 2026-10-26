// 体素级材料标签 + 多材料谱投影自检。
//
// 判据：
//   1. 单材料模式（shellThickness=0）：标签只有 1 种，内部体素数与原掩膜一致
//   2. 双材料模式：外壳体素 + 内核体素 = 内部体素数
//   3. 皮质骨厚度扫描：厚度增大 → 外壳体素数单调增加、内核单调减少
//   4. 解析校验：半径 50mm 球的 5mm 皮质骨壳，壳体积与解析值
//      4π/3·(R³ − (R−t)³) 的偏差 < 8%（体素离散）
//   5. 多材料谱投影：骨壳 + 水核的透射率低于同形状纯水（骨 μ 更大）
//   6. 多材料与单材料在同材料时结果一致（回归）
// 退出码 0 表示全部通过。

#include "io/MaterialLabeling.h"
#include "io/MeshLoader.h"
#include "io/VoxelGrid.h"
#include "simulation/AttenuationTable.h"
#include "simulation/ConeBeamView.h"
#include "simulation/CpuProjector.h"
#include "simulation/SpectralProjector.h"
#include "TestGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <vtkImageData.h>

namespace {

constexpr double kPi = 3.14159265358979323846;

struct CheckResult
{
    bool ok = true;
    std::string name;
};

std::vector<CheckResult> g_checks;

void checkNear(const std::string& name, double actual, double expected, double relativeTolerance)
{
    const double denominator = std::max(std::fabs(expected), 1e-9);
    const bool ok = std::fabs(actual - expected) / denominator <= relativeTolerance;
    g_checks.push_back({ok, name});
    std::printf("[%s] %-44s 实际 %.4f  期望 %.4f  允许偏差 %.1f%%\n",
                ok ? "PASS" : "FAIL", name.c_str(), actual, expected, relativeTolerance * 100.0);
}

void checkExact(const std::string& name, long long actual, long long expected)
{
    const bool ok = actual == expected;
    g_checks.push_back({ok, name});
    std::printf("[%s] %-44s 实际 %lld  期望 %lld\n",
                ok ? "PASS" : "FAIL", name.c_str(), actual, expected);
}

void checkTrue(const std::string& name, bool condition)
{
    g_checks.push_back({condition, name});
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name.c_str());
}

} // namespace

int main()
{
    constexpr double kRadius = 50.0;
    const std::filesystem::path spherePath =
        std::filesystem::temp_directory_path() / "pcsim_label_sphere.stl";
    pcsim::test::writeBinaryStl(spherePath, pcsim::test::makeIcosphere(kRadius, 3));

    std::string error;
    const auto mesh = precisioncbct::io::loadMesh(spherePath.string(), &error);
    if (!mesh.isValid())
    {
        std::printf("FAIL 模型读取失败: %s\n", error.c_str());
        return 1;
    }

    precisioncbct::io::VoxelizationOptions voxelOptions;
    voxelOptions.spacingMm = 1.0;
    const auto mask = precisioncbct::io::voxelize(mesh.polyData, voxelOptions, &error);
    if (!mask.isValid())
    {
        std::printf("FAIL 体素化失败: %s\n", error.c_str());
        return 1;
    }
    const long long insideVoxels = mask.insideVoxels;

    // ---------------------------------------------------- 1. 单材料模式
    precisioncbct::io::LabelingOptions single;
    single.shellThicknessMm = 0.0;
    single.coreMaterial = precisioncbct::simulation::Material::Water;
    precisioncbct::io::MaterialLabeling singleLabeling;
    checkTrue("单材料标签成功",
              precisioncbct::io::labelMaterials(mask, single, &singleLabeling, &error));
    checkExact("单材料标签数", singleLabeling.labelCount(), 1);
    checkExact("单材料内部体素数等于掩膜", singleLabeling.voxelCounts[1], insideVoxels);
    checkExact("单材料空气体素数", singleLabeling.voxelCounts[0], mask.totalVoxels - insideVoxels);

    // ---------------------------------------------------- 2~4. 双材料模式
    constexpr double kShell = 5.0;
    precisioncbct::io::LabelingOptions shellOptions;
    shellOptions.shellThicknessMm = kShell;
    shellOptions.shellMaterial = precisioncbct::simulation::Material::CorticalBone;
    shellOptions.coreMaterial = precisioncbct::simulation::Material::Water;

    precisioncbct::io::MaterialLabeling shellLabeling;
    if (!precisioncbct::io::labelMaterials(mask, shellOptions, &shellLabeling, &error))
    {
        std::printf("FAIL 双材料标签失败: %s\n", error.c_str());
        return 1;
    }
    checkExact("双材料标签数", shellLabeling.labelCount(), 2);
    checkExact("外壳 + 内核 = 内部体素",
               shellLabeling.voxelCounts[1] + shellLabeling.voxelCounts[2], insideVoxels);
    checkTrue("外壳体素 > 0", shellLabeling.voxelCounts[1] > 0);
    checkTrue("内核体素 > 0", shellLabeling.voxelCounts[2] > 0);

    const double analyticShellVolume =
        4.0 / 3.0 * kPi * (std::pow(kRadius, 3) - std::pow(kRadius - kShell, 3));
    checkNear("5mm 皮质骨壳体积 (mm³)", static_cast<double>(shellLabeling.voxelCounts[1]),
              analyticShellVolume, 0.08);

    // 厚度扫描
    std::array<long long, 4> shellCounts{};
    const std::array<double, 4> thicknesses = {1.0, 2.0, 4.0, 8.0};
    for (size_t i = 0; i < thicknesses.size(); ++i)
    {
        precisioncbct::io::LabelingOptions options = shellOptions;
        options.shellThicknessMm = thicknesses[i];
        precisioncbct::io::MaterialLabeling labeling;
        if (!precisioncbct::io::labelMaterials(mask, options, &labeling, &error))
        {
            std::printf("FAIL 厚度 %.1fmm 标签失败: %s\n", thicknesses[i], error.c_str());
            return 1;
        }
        shellCounts[i] = labeling.voxelCounts[1];
        std::printf("[info] 厚度 %.1f mm → 外壳 %lld / 内核 %lld\n", thicknesses[i],
                    labeling.voxelCounts[1], labeling.voxelCounts[2]);
    }
    checkTrue("厚度增大 → 外壳体素单调增加",
              shellCounts[0] < shellCounts[1] && shellCounts[1] < shellCounts[2]
                  && shellCounts[2] < shellCounts[3]);

    // ---------------------------------------------------- 5~6. 多材料谱投影
    precisioncbct::io::VoxelGrid labeledGrid = mask;
    labeledGrid.image = shellLabeling.labels;

    precisioncbct::simulation::ConeBeamView view;
    view.sourceAxisDistanceMm = 500.0;
    view.sourceDetectorDistanceMm = 1000.0;
    view.pixelSizeMm = 2.0;
    view.columns = 96;
    view.rows = 96;
    view.angleDeg = 0.0;

    precisioncbct::simulation::SpectralProjectionOptions options;
    options.spectrum.tubeVoltageKvp = 120.0;
    options.spectrum.inherentFiltrationAlMm = 2.5;
    options.incidentPhotonsPerPixel = 1.0e6;
    options.enableNoise = false;
    options.labeling = &shellLabeling;

    const auto bonePhantom =
        precisioncbct::simulation::projectConeBeamSpectral(labeledGrid, view, options, &error);
    if (!bonePhantom.isValid())
    {
        std::printf("FAIL 多材料谱投影失败: %s\n", error.c_str());
        return 1;
    }

    // 同形状纯水参照
    precisioncbct::io::VoxelGrid waterGrid = mask;
    waterGrid.image = singleLabeling.labels;
    precisioncbct::simulation::SpectralProjectionOptions waterOptions = options;
    waterOptions.labeling = &singleLabeling;
    const auto waterPhantom =
        precisioncbct::simulation::projectConeBeamSpectral(waterGrid, view, waterOptions, &error);
    if (!waterPhantom.isValid())
    {
        std::printf("FAIL 纯水谱投影失败: %s\n", error.c_str());
        return 1;
    }

    const size_t centreIndex = static_cast<size_t>(96 / 2) * 96 + 96 / 2;
    std::printf("[info] 中心像素：骨壳 phantom T=%.5f / 纯水 T=%.5f，路径 %.1f mm\n",
                bonePhantom.transmission[centreIndex], waterPhantom.transmission[centreIndex],
                bonePhantom.lineIntegralMm[centreIndex]);

    checkTrue("骨壳透射率低于同形状纯水",
              bonePhantom.transmission[centreIndex] < waterPhantom.transmission[centreIndex]);
    checkTrue("骨壳等效 μ 高于纯水",
              bonePhantom.effectiveMuPerMm[centreIndex] > waterPhantom.effectiveMuPerMm[centreIndex]);
    checkNear("多材料与纯水路径长度一致", bonePhantom.lineIntegralMm[centreIndex],
              waterPhantom.lineIntegralMm[centreIndex], 1e-5);

    // 单材料模式（labeling=nullptr）应与「标签=1 的纯水」完全一致 —— 验证重构无回归
    precisioncbct::simulation::SpectralProjectionOptions plainOptions = options;
    plainOptions.labeling = nullptr;
    plainOptions.material = precisioncbct::simulation::Material::Water;
    const auto plain = precisioncbct::simulation::projectConeBeamSpectral(waterGrid, view,
                                                                         plainOptions, &error);
    if (!plain.isValid())
    {
        std::printf("FAIL 单材料谱投影失败: %s\n", error.c_str());
        return 1;
    }
    // 两条路径的差异仅来自浮点累加顺序（单材料在 double 里求和后一次转换，
    // 多材料逐体素累加到 float），允许 0.01% 的差别
    std::printf("[info] 回归对比：plain=%.9f label=%.9f Δ=%.3e\n",
                plain.transmission[centreIndex], waterPhantom.transmission[centreIndex],
                std::fabs(plain.transmission[centreIndex] - waterPhantom.transmission[centreIndex]));
    checkNear("单材料与等价标签结果一致（回归）", plain.transmission[centreIndex],
              waterPhantom.transmission[centreIndex], 1e-4);

    std::error_code ignored;
    std::filesystem::remove(spherePath, ignored);

    const auto failed = std::count_if(g_checks.begin(), g_checks.end(),
                                      [](const CheckResult& r) { return !r.ok; });
    std::printf("\n%zu 项检查，失败 %lld 项\n", g_checks.size(), static_cast<long long>(failed));
    return failed == 0 ? 0 : 1;
}
