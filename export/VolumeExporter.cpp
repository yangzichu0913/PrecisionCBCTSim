#include "export/VolumeExporter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace precisioncbct::exporter {
namespace {

constexpr int kSliceDigits = 4;

/// 找出给定百分位对应的值（用于自动窗宽窗位）
double percentile(const std::vector<float>& values, double fraction)
{
    if (values.empty())
    {
        return 0.0;
    }
    std::vector<float> sorted = values;
    const size_t index = static_cast<size_t>(
        std::clamp(fraction, 0.0, 1.0) * static_cast<double>(sorted.size() - 1));
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<long>(index), sorted.end());
    return static_cast<double>(sorted[index]);
}

} // namespace

bool writeVolumeSlices(const simulation::ReconstructedVolume& volume,
                       const std::string& directory,
                       double windowMin,
                       double windowMax,
                       std::string* error)
{
    if (!volume.isValid())
    {
        if (error) { *error = "重建体数据无效，无法导出"; }
        return false;
    }
    if (!(windowMax > windowMin))
    {
        windowMax = windowMin + 1e-9;
    }

    const QString dirPath = QString::fromStdString(directory);
    QDir dir(dirPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
    {
        if (error) { *error = "无法创建导出目录：" + directory; }
        return false;
    }

    const int width = volume.dimensions[0];
    const int height = volume.dimensions[1];
    const int depth = volume.dimensions[2];
    const double scale = 65535.0 / (windowMax - windowMin);

    std::vector<std::uint16_t> pixels(static_cast<size_t>(width) * height);

    for (int k = 0; k < depth; ++k)
    {
        for (int j = 0; j < height; ++j)
        {
            for (int i = 0; i < width; ++i)
            {
                // 图像行对应体数据的 j，列对应 i（与显示一致）
                const double value = volume.at(i, j, k);
                const double normalized = (value - windowMin) * scale;
                pixels[static_cast<size_t>(j) * width + i] =
                    static_cast<std::uint16_t>(std::clamp(normalized, 0.0, 65535.0));
            }
        }

        QImage image(reinterpret_cast<const uchar*>(pixels.data()), width, height,
                     width * static_cast<int>(sizeof(std::uint16_t)),
                     QImage::Format_Grayscale16);
        if (image.isNull())
        {
            if (error) { *error = "无法构造 16 位灰度图像"; }
            return false;
        }

        const QString fileName =
            QStringLiteral("slice_%1.png").arg(k, kSliceDigits, 10, QLatin1Char('0'));
        const QString path = dir.filePath(fileName);
        if (!image.copy().save(path, "PNG"))
        {
            if (error) { *error = "写入 PNG 失败：" + path.toStdString(); }
            return false;
        }
    }

    // JSON 头：写明几何与量化，便于脚本还原物理量纲
    QJsonObject header;
    header[QStringLiteral("dimensions")] = QStringLiteral("%1x%2x%3").arg(width).arg(height).arg(depth);
    header[QStringLiteral("spacingMm")] = QStringLiteral("%1,%2,%3")
                                              .arg(volume.spacingMm[0], 0, 'g', 6)
                                              .arg(volume.spacingMm[1], 0, 'g', 6)
                                              .arg(volume.spacingMm[2], 0, 'g', 6);
    header[QStringLiteral("originMm")] = QStringLiteral("%1,%2,%3")
                                             .arg(volume.originMm[0], 0, 'g', 6)
                                             .arg(volume.originMm[1], 0, 'g', 6)
                                             .arg(volume.originMm[2], 0, 'g', 6);
    header[QStringLiteral("valueUnit")] = QStringLiteral("1/mm");
    header[QStringLiteral("windowMin")] = windowMin;
    header[QStringLiteral("windowMax")] = windowMax;
    header[QStringLiteral("rows")] = QStringLiteral("axial (z) slices, index = file index");
    header[QStringLiteral("slicePattern")] = QStringLiteral("slice_%1.png").arg(0, kSliceDigits, 10, QLatin1Char('0'));

    const QByteArray json = QJsonDocument(header).toJson(QJsonDocument::Indented);
    QFile headerFile(dir.filePath(QStringLiteral("volume.json")));
    if (!headerFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (error) { *error = "无法写入 volume.json"; }
        return false;
    }
    headerFile.write(json);
    headerFile.close();
    return true;
}

bool writeVolumeSlices(const simulation::ReconstructedVolume& volume,
                       const std::string& directory,
                       std::string* error)
{
    if (!volume.isValid())
    {
        if (error) { *error = "重建体数据无效，无法导出"; }
        return false;
    }
    // 0.5% / 99.5% 百分位：避开少量高值伪影（锥束边缘振铃）主导窗位
    const double low = percentile(volume.attenuationPerMm, 0.005);
    const double high = percentile(volume.attenuationPerMm, 0.995);
    return writeVolumeSlices(volume, directory, std::max(0.0, low), high, error);
}

} // namespace precisioncbct::exporter
