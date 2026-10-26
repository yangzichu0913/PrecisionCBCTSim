#include "export/DrExporter.h"

#include "simulation/CpuProjector.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace precisioncbct::exporter {

QImage makeDisplayImage(const simulation::DrProjection& projection)
{
    if (!projection.isValid())
    {
        return QImage();
    }

    const auto [minIt, maxIt] = std::minmax_element(projection.transmission.begin(),
                                                    projection.transmission.end());
    const float minValue = *minIt;
    const float maxValue = *maxIt;
    const float span = (maxValue > minValue) ? (maxValue - minValue) : 1.0f;

    QImage image(projection.width, projection.height, QImage::Format_Grayscale8);
    for (int y = 0; y < projection.height; ++y)
    {
        uchar* line = image.scanLine(y);
        const float* source = projection.transmission.data()
                              + static_cast<size_t>(y) * projection.width;
        for (int x = 0; x < projection.width; ++x)
        {
            const float normalized = (source[x] - minValue) / span;
            line[x] = static_cast<uchar>(std::lround(std::clamp(normalized, 0.0f, 1.0f) * 255.0f));
        }
    }
    return image;
}

bool writeDrPng16(const simulation::DrProjection& projection, const QString& path, QString* error)
{
    if (!projection.isValid())
    {
        if (error) { *error = QStringLiteral("没有可导出的 DR 图像"); }
        return false;
    }
    if (path.isEmpty())
    {
        if (error) { *error = QStringLiteral("导出路径为空"); }
        return false;
    }

    QImage image(projection.width, projection.height, QImage::Format_Grayscale16);
    if (image.isNull())
    {
        if (error) { *error = QStringLiteral("无法创建 16 位图像缓冲"); }
        return false;
    }

    for (int y = 0; y < projection.height; ++y)
    {
        auto* line = reinterpret_cast<quint16*>(image.scanLine(y));
        const float* source = projection.transmission.data()
                              + static_cast<size_t>(y) * projection.width;
        for (int x = 0; x < projection.width; ++x)
        {
            const float clamped = std::clamp(source[x], 0.0f, 1.0f);
            line[x] = static_cast<quint16>(std::lround(clamped * 65535.0f));
        }
    }

    if (!image.save(path, "PNG"))
    {
        if (error) { *error = QStringLiteral("写入 PNG 失败：%1").arg(path); }
        return false;
    }
    return true;
}

} // namespace precisioncbct::exporter
