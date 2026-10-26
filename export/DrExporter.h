#pragma once

#include <QImage>
#include <QString>

namespace precisioncbct::simulation {
struct DrProjection;
}

namespace precisioncbct::exporter {

/// 生成用于界面显示的 8 位灰度图。
///
/// DR 的透射率动态范围通常很窄（例如 0.13~0.15），直接显示几乎全黑，
/// 因此显示时按 [min, max] 线性拉伸；导出时保持原始物理量。
QImage makeDisplayImage(const simulation::DrProjection& projection);

/// 以 16 位灰度 PNG 导出 DR（0 = 全吸收，65535 = 无衰减 I0）。
bool writeDrPng16(const simulation::DrProjection& projection, const QString& path, QString* error);

} // namespace precisioncbct::exporter
