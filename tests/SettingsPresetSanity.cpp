// 参数预设（JSON 保存/加载）的回归测试。
//
// 判据：
//  1. 默认预设往返序列化后每个字段都不变；
//  2. 自定义参数（含探测器模型）同样往返一致；
//  3. 缺失字段沿用调用方原值，未知字段被忽略；
//  4. 非法 JSON、越界参数、未知材料、过新版本都被拒绝；
//  5. 落盘 → 读回的端到端路径可用。

#include "settings/SettingsPreset.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <QDir>
#include <QTemporaryDir>

namespace {

int g_failures = 0;

void check(bool condition, const char* name)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", name);
    if (!condition)
    {
        ++g_failures;
    }
}

void checkNear(double actual, double expected, double tolerance, const char* name)
{
    const double diff = std::fabs(actual - expected);
    const bool ok = diff <= tolerance * std::max(1.0, std::fabs(expected));
    std::printf("%s %-42s 实际 %.6f  期望 %.6f  允许偏差 %.1f%%\n",
                ok ? "[PASS]" : "[FAIL]", name, actual, expected, tolerance * 100.0);
    if (!ok)
    {
        ++g_failures;
    }
}

precisioncbct::settings::SimulationPreset makeCustomPreset()
{
    using namespace precisioncbct::simulation;

    precisioncbct::settings::SimulationPreset preset;
    preset.scan.sourceAxisDistanceMm = 620.0;
    preset.scan.sourceDetectorDistanceMm = 1150.0;
    preset.scan.detectorPixelSizeMm = 0.28;
    preset.scan.detectorColumns = 768;
    preset.scan.detectorRows = 512;
    preset.scan.startAngleDeg = 30.0;
    preset.scan.endAngleDeg = 210.0;
    preset.scan.angleStepDeg = 0.5;

    preset.spectral.enabled = true;
    preset.spectral.tubeVoltageKvp = 120.0;
    preset.spectral.inherentFiltrationAlMm = 3.5;
    preset.spectral.incidentPhotonsPerPixel = 25000.0;
    preset.spectral.enableNoise = false;
    preset.spectral.seed = 123456u;
    preset.spectral.material = Material::CorticalBone;
    preset.spectral.detector.scatterToPrimaryRatio = 0.42;
    preset.spectral.detector.scatterBlurSigmaPixels = 14.0;
    preset.spectral.detector.detectorBlurSigmaPixels = 1.6;
    preset.spectral.detector.crosstalkFraction = 0.08;
    preset.spectral.detector.electronicNoisePhotons = 137.0;

    preset.shellThicknessMm = 2.5;
    preset.voxelSizeMm = 0.6;
    return preset;
}

void checkPresetEqual(const precisioncbct::settings::SimulationPreset& a,
                      const precisioncbct::settings::SimulationPreset& b)
{
    checkNear(a.scan.sourceAxisDistanceMm, b.scan.sourceAxisDistanceMm, 1e-12, "SAD 一致");
    checkNear(a.scan.sourceDetectorDistanceMm, b.scan.sourceDetectorDistanceMm, 1e-12, "SDD 一致");
    checkNear(a.scan.detectorPixelSizeMm, b.scan.detectorPixelSizeMm, 1e-12, "像素尺寸一致");
    check(a.scan.detectorColumns == b.scan.detectorColumns, "探测器列数一致");
    check(a.scan.detectorRows == b.scan.detectorRows, "探测器行数一致");
    checkNear(a.scan.startAngleDeg, b.scan.startAngleDeg, 1e-12, "起始角一致");
    checkNear(a.scan.endAngleDeg, b.scan.endAngleDeg, 1e-12, "终止角一致");
    checkNear(a.scan.angleStepDeg, b.scan.angleStepDeg, 1e-12, "角度步长一致");
    check(a.spectral.enabled == b.spectral.enabled, "能谱开关一致");
    checkNear(a.spectral.tubeVoltageKvp, b.spectral.tubeVoltageKvp, 1e-12, "管电压一致");
    checkNear(a.spectral.inherentFiltrationAlMm, b.spectral.inherentFiltrationAlMm, 1e-12,
              "铝滤过一致");
    checkNear(a.spectral.incidentPhotonsPerPixel, b.spectral.incidentPhotonsPerPixel, 1e-12,
              "入射光子数一致");
    check(a.spectral.enableNoise == b.spectral.enableNoise, "量子噪声开关一致");
    check(a.spectral.seed == b.spectral.seed, "随机种子一致");
    check(a.spectral.material == b.spectral.material, "材料一致");
    checkNear(a.spectral.detector.scatterToPrimaryRatio,
              b.spectral.detector.scatterToPrimaryRatio, 1e-12, "SPR 一致");
    checkNear(a.spectral.detector.scatterBlurSigmaPixels,
              b.spectral.detector.scatterBlurSigmaPixels, 1e-12, "散射核一致");
    checkNear(a.spectral.detector.detectorBlurSigmaPixels,
              b.spectral.detector.detectorBlurSigmaPixels, 1e-12, "探测器 PSF 一致");
    checkNear(a.spectral.detector.crosstalkFraction, b.spectral.detector.crosstalkFraction, 1e-12,
              "串扰比例一致");
    checkNear(a.spectral.detector.electronicNoisePhotons, b.spectral.detector.electronicNoisePhotons,
              1e-12, "电子学噪声一致");
    checkNear(a.shellThicknessMm, b.shellThicknessMm, 1e-12, "皮质骨厚度一致");
    checkNear(a.voxelSizeMm, b.voxelSizeMm, 1e-12, "体素尺寸一致");
}

} // namespace

int main()
{
    using namespace precisioncbct::settings;

    // 1. 默认预设往返
    {
        const SimulationPreset original;
        const std::string json = toJson(original);
        SimulationPreset parsed;
        std::string error;
        check(fromJson(json, &parsed, &error), "默认预设往返解析成功");
        checkPresetEqual(original, parsed);
    }

    // 2. 自定义预设往返（含探测器模型）
    {
        const SimulationPreset original = makeCustomPreset();
        const std::string json = toJson(original);
        check(json.find("\"angleStepDeg\"") != std::string::npos, "JSON 含 angleStepDeg 字段");
        check(json.find("\"detector\"") != std::string::npos, "JSON 含 detector 段落");

        SimulationPreset parsed;
        std::string error;
        check(fromJson(json, &parsed, &error), "自定义预设往返解析成功");
        checkPresetEqual(original, parsed);
    }

    // 3. 缺失字段沿用原值；未知字段忽略
    {
        SimulationPreset preset = makeCustomPreset();
        SimulationPreset seed = preset;
        seed.scan.detectorColumns = 100;   // 故意造一个与预设不同的初值
        seed.spectral.tubeVoltageKvp = 70.0;
        std::string error;
        check(fromJson("{\"unknownKey\": 1}", &seed, &error), "未知字段被忽略");
        check(seed.scan.detectorColumns == 100, "缺失字段沿用调用方原值（列数）");
        checkNear(seed.spectral.tubeVoltageKvp, 70.0, 1e-12, "缺失字段沿用调用方原值（kVp）");
    }

    // 4. 非法输入被拒绝
    {
        SimulationPreset preset;
        std::string error;
        check(!fromJson("{ this is not json", &preset, &error), "非法 JSON 被拒绝");
        check(!error.empty(), "非法 JSON 填写了错误信息");

        SimulationPreset bad = makeCustomPreset();
        bad.scan.sourceDetectorDistanceMm = bad.scan.sourceAxisDistanceMm - 10.0;
        check(!fromJson(toJson(bad), &preset, &error), "SDD <= SAD 被拒绝");

        bad = makeCustomPreset();
        bad.scan.angleStepDeg = 0.0;
        check(!fromJson(toJson(bad), &preset, &error), "角度步长 0 被拒绝");

        bad = makeCustomPreset();
        bad.spectral.detector.crosstalkFraction = 0.6;
        check(!fromJson(toJson(bad), &preset, &error), "串扰比例 0.6 被拒绝");

        bad = makeCustomPreset();
        bad.spectral.detector.electronicNoisePhotons = -5.0;
        check(!fromJson(toJson(bad), &preset, &error), "负的电子学噪声被拒绝");

        bad = makeCustomPreset();
        bad.spectral.incidentPhotonsPerPixel = 0.0;
        check(!fromJson(toJson(bad), &preset, &error), "入射光子数 0 被拒绝");
    }

    // 5. 未知材料与过新版本
    {
        SimulationPreset preset;
        std::string error;
        const std::string unknownMaterial =
            "{\"formatVersion\": 1, \"spectral\": {\"material\": \"Unobtainium\"}}";
        check(!fromJson(unknownMaterial, &preset, &error), "未知材料被拒绝");
        check(error.find("Unobtainium") != std::string::npos, "错误信息包含未知材料名");

        const std::string futureVersion = "{\"formatVersion\": 99}";
        check(!fromJson(futureVersion, &preset, &error), "过新版本被拒绝");
    }

    // 6. 落盘 → 读回
    {
        QTemporaryDir dir;
        check(dir.isValid(), "临时目录创建成功");
        const std::string path =
            (QDir(dir.path()).filePath(QStringLiteral("preset.json"))).toStdString();

        const SimulationPreset original = makeCustomPreset();
        std::string error;
        check(saveToFile(path, original, &error), "预设写入文件成功");

        SimulationPreset loaded;
        check(loadFromFile(path, &loaded, &error), "预设从文件读取成功");
        checkPresetEqual(original, loaded);

        SimulationPreset unused;
        check(!loadFromFile(path + ".missing", &unused, &error), "缺失文件返回失败");
    }

    std::printf("\n%s：失败 %d 项\n", g_failures == 0 ? "全部通过" : "存在失败", g_failures);
    return g_failures == 0 ? 0 : 1;
}
