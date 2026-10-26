#include "settings/SettingsPreset.h"

#include <QByteArray>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include <cmath>

namespace precisioncbct::settings {
namespace {

constexpr int kFormatVersion = 1;

// JSON 字段名集中在这里，测试与外部脚本可以据此书写预设文件
constexpr char kKeyFormatVersion[] = "formatVersion";
constexpr char kKeyScan[] = "scan";
constexpr char kKeySpectral[] = "spectral";
constexpr char kKeyDetector[] = "detector";
constexpr char kKeyShellThickness[] = "shellThicknessMm";
constexpr char kKeyVoxelSize[] = "voxelSizeMm";

QJsonObject detectorToJson(const simulation::DetectorModelOptions& detector)
{
    QJsonObject object;
    object[QStringLiteral("scatterToPrimaryRatio")] = detector.scatterToPrimaryRatio;
    object[QStringLiteral("scatterBlurSigmaPixels")] = detector.scatterBlurSigmaPixels;
    object[QStringLiteral("detectorBlurSigmaPixels")] = detector.detectorBlurSigmaPixels;
    object[QStringLiteral("crosstalkFraction")] = detector.crosstalkFraction;
    object[QStringLiteral("electronicNoisePhotons")] = detector.electronicNoisePhotons;
    return object;
}

void detectorFromJson(const QJsonObject& object, simulation::DetectorModelOptions* detector)
{
    if (object.contains(QStringLiteral("scatterToPrimaryRatio")))
    {
        detector->scatterToPrimaryRatio = object.value(QStringLiteral("scatterToPrimaryRatio")).toDouble();
    }
    if (object.contains(QStringLiteral("scatterBlurSigmaPixels")))
    {
        detector->scatterBlurSigmaPixels = object.value(QStringLiteral("scatterBlurSigmaPixels")).toDouble();
    }
    if (object.contains(QStringLiteral("detectorBlurSigmaPixels")))
    {
        detector->detectorBlurSigmaPixels =
            object.value(QStringLiteral("detectorBlurSigmaPixels")).toDouble();
    }
    if (object.contains(QStringLiteral("crosstalkFraction")))
    {
        detector->crosstalkFraction = object.value(QStringLiteral("crosstalkFraction")).toDouble();
    }
    if (object.contains(QStringLiteral("electronicNoisePhotons")))
    {
        detector->electronicNoisePhotons =
            object.value(QStringLiteral("electronicNoisePhotons")).toDouble();
    }
}

/// 参数合法性校验：与命令行/界面保持同一套约束，避免加载出自相矛盾的预设
bool validate(const SimulationPreset& preset, std::string* error)
{
    const simulation::ScanParameters& scan = preset.scan;
    const simulation::SpectralSettings& spectral = preset.spectral;
    const auto fail = [error](const char* message) {
        if (error) { *error = message; }
        return false;
    };

    if (!(scan.sourceAxisDistanceMm > 0.0) || !(scan.sourceDetectorDistanceMm > scan.sourceAxisDistanceMm))
    {
        return fail("SAD/SDD 非法：需满足 0 < SAD < SDD");
    }
    if (!(scan.detectorPixelSizeMm > 0.0) || scan.detectorColumns < 1 || scan.detectorRows < 1)
    {
        return fail("探测器参数非法：像素尺寸与行列数必须大于 0");
    }
    if (!(scan.angleStepDeg > 0.0))
    {
        return fail("角度步长必须大于 0");
    }
    if (!(spectral.tubeVoltageKvp > 10.0))
    {
        return fail("管电压必须大于 10 kV");
    }
    if (!(spectral.incidentPhotonsPerPixel > 0.0))
    {
        return fail("每像素入射光子数必须大于 0");
    }
    if (!(spectral.detector.crosstalkFraction >= 0.0 && spectral.detector.crosstalkFraction < 0.5))
    {
        return fail("串扰比例需在 [0, 0.5) 之间");
    }
    if (spectral.detector.scatterToPrimaryRatio < 0.0
        || spectral.detector.detectorBlurSigmaPixels < 0.0
        || spectral.detector.scatterBlurSigmaPixels < 0.0
        || spectral.detector.electronicNoisePhotons < 0.0)
    {
        return fail("探测器模型参数不能为负");
    }
    if (preset.shellThicknessMm < 0.0)
    {
        return fail("皮质骨厚度不能为负");
    }
    if (!(preset.voxelSizeMm > 0.0))
    {
        return fail("体素尺寸必须大于 0");
    }
    return true;
}

} // namespace

std::string toJson(const SimulationPreset& preset)
{
    QJsonObject scan;
    scan[QStringLiteral("sourceAxisDistanceMm")] = preset.scan.sourceAxisDistanceMm;
    scan[QStringLiteral("sourceDetectorDistanceMm")] = preset.scan.sourceDetectorDistanceMm;
    scan[QStringLiteral("detectorPixelSizeMm")] = preset.scan.detectorPixelSizeMm;
    scan[QStringLiteral("detectorColumns")] = preset.scan.detectorColumns;
    scan[QStringLiteral("detectorRows")] = preset.scan.detectorRows;
    scan[QStringLiteral("startAngleDeg")] = preset.scan.startAngleDeg;
    scan[QStringLiteral("endAngleDeg")] = preset.scan.endAngleDeg;
    scan[QStringLiteral("angleStepDeg")] = preset.scan.angleStepDeg;

    QJsonObject spectral;
    spectral[QStringLiteral("enabled")] = preset.spectral.enabled;
    spectral[QStringLiteral("tubeVoltageKvp")] = preset.spectral.tubeVoltageKvp;
    spectral[QStringLiteral("inherentFiltrationAlMm")] = preset.spectral.inherentFiltrationAlMm;
    spectral[QStringLiteral("incidentPhotonsPerPixel")] = preset.spectral.incidentPhotonsPerPixel;
    spectral[QStringLiteral("enableNoise")] = preset.spectral.enableNoise;
    spectral[QStringLiteral("seed")] = static_cast<double>(preset.spectral.seed);
    spectral[QStringLiteral("material")] =
        QString::fromLatin1(simulation::materialName(preset.spectral.material));
    spectral[QString::fromLatin1(kKeyDetector)] = detectorToJson(preset.spectral.detector);

    QJsonObject root;
    root[QString::fromLatin1(kKeyFormatVersion)] = kFormatVersion;
    root[QString::fromLatin1(kKeyScan)] = scan;
    root[QString::fromLatin1(kKeySpectral)] = spectral;
    root[QString::fromLatin1(kKeyShellThickness)] = preset.shellThicknessMm;
    root[QString::fromLatin1(kKeyVoxelSize)] = preset.voxelSizeMm;
    root[QStringLiteral("scannerMode")] = QString::fromStdString(preset.scannerMode);

    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    return std::string(json.constData(), static_cast<size_t>(json.size()));
}

bool fromJson(const std::string& text, SimulationPreset* preset, std::string* error)
{
    if (preset == nullptr)
    {
        if (error) { *error = "内部错误：预设指针为空"; }
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray(text.data(), static_cast<int>(text.size())), &parseError);
    if (document.isNull() || !document.isObject())
    {
        if (error)
        {
            *error = "预设文件不是合法 JSON：" + parseError.errorString().toStdString();
        }
        return false;
    }

    const QJsonObject root = document.object();
    const int version = root.value(QString::fromLatin1(kKeyFormatVersion)).toInt(kFormatVersion);
    if (version > kFormatVersion)
    {
        if (error) { *error = "预设文件版本过新（" + std::to_string(version) + "）"; }
        return false;
    }

    if (root.value(QString::fromLatin1(kKeyScan)).isObject())
    {
        const QJsonObject scan = root.value(QString::fromLatin1(kKeyScan)).toObject();
        const auto number = [&scan](const char* key, double fallback) {
            const QJsonValue value = scan.value(QString::fromLatin1(key));
            return value.isDouble() ? value.toDouble() : fallback;
        };
        const auto integer = [&scan](const char* key, int fallback) {
            const QJsonValue value = scan.value(QString::fromLatin1(key));
            return value.isDouble() ? value.toInt() : fallback;
        };
        preset->scan.sourceAxisDistanceMm =
            number("sourceAxisDistanceMm", preset->scan.sourceAxisDistanceMm);
        preset->scan.sourceDetectorDistanceMm =
            number("sourceDetectorDistanceMm", preset->scan.sourceDetectorDistanceMm);
        preset->scan.detectorPixelSizeMm =
            number("detectorPixelSizeMm", preset->scan.detectorPixelSizeMm);
        preset->scan.detectorColumns = integer("detectorColumns", preset->scan.detectorColumns);
        preset->scan.detectorRows = integer("detectorRows", preset->scan.detectorRows);
        preset->scan.startAngleDeg = number("startAngleDeg", preset->scan.startAngleDeg);
        preset->scan.endAngleDeg = number("endAngleDeg", preset->scan.endAngleDeg);
        preset->scan.angleStepDeg = number("angleStepDeg", preset->scan.angleStepDeg);
    }

    if (root.value(QString::fromLatin1(kKeySpectral)).isObject())
    {
        const QJsonObject spectral = root.value(QString::fromLatin1(kKeySpectral)).toObject();
        const QJsonValue enabled = spectral.value(QStringLiteral("enabled"));
        if (enabled.isBool()) { preset->spectral.enabled = enabled.toBool(); }

        const QJsonValue kvp = spectral.value(QStringLiteral("tubeVoltageKvp"));
        if (kvp.isDouble()) { preset->spectral.tubeVoltageKvp = kvp.toDouble(); }

        const QJsonValue filtration = spectral.value(QStringLiteral("inherentFiltrationAlMm"));
        if (filtration.isDouble()) { preset->spectral.inherentFiltrationAlMm = filtration.toDouble(); }

        const QJsonValue photons = spectral.value(QStringLiteral("incidentPhotonsPerPixel"));
        if (photons.isDouble()) { preset->spectral.incidentPhotonsPerPixel = photons.toDouble(); }

        const QJsonValue noise = spectral.value(QStringLiteral("enableNoise"));
        if (noise.isBool()) { preset->spectral.enableNoise = noise.toBool(); }

        const QJsonValue seed = spectral.value(QStringLiteral("seed"));
        if (seed.isDouble()) { preset->spectral.seed = static_cast<std::uint32_t>(seed.toDouble()); }

        const QJsonValue material = spectral.value(QStringLiteral("material"));
        if (material.isString())
        {
            simulation::Material parsed = preset->spectral.material;
            if (simulation::parseMaterial(material.toString().toStdString(), &parsed))
            {
                preset->spectral.material = parsed;
            }
            else
            {
                if (error)
                {
                    *error = "未知材料：" + material.toString().toStdString();
                }
                return false;
            }
        }

        const QJsonValue detector = spectral.value(QString::fromLatin1(kKeyDetector));
        if (detector.isObject())
        {
            detectorFromJson(detector.toObject(), &preset->spectral.detector);
        }
    }

    const QJsonValue shell = root.value(QString::fromLatin1(kKeyShellThickness));
    if (shell.isDouble()) { preset->shellThicknessMm = shell.toDouble(); }

    const QJsonValue voxel = root.value(QString::fromLatin1(kKeyVoxelSize));
    if (voxel.isDouble()) { preset->voxelSizeMm = voxel.toDouble(); }

    const QJsonValue mode = root.value(QStringLiteral("scannerMode"));
    if (mode.isString())
    {
        preset->scannerMode = mode.toString().toStdString();
    }

    return validate(*preset, error);
}

bool saveToFile(const std::string& path, const SimulationPreset& preset, std::string* error)
{
    if (!validate(preset, error))
    {
        return false;
    }

    const std::string text = toJson(preset);
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (error) { *error = "无法写入预设文件：" + path; }
        return false;
    }
    file.write(text.data(), static_cast<qint64>(text.size()));
    file.close();
    return true;
}

bool loadFromFile(const std::string& path, SimulationPreset* preset, std::string* error)
{
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error) { *error = "无法读取预设文件：" + path; }
        return false;
    }
    const QByteArray content = file.readAll();
    file.close();
    return fromJson(std::string(content.constData(), static_cast<size_t>(content.size())),
                    preset, error);
}

} // namespace precisioncbct::settings
