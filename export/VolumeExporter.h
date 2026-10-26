#pragma once

#include <string>

#include "simulation/FdkReconstructor.h"

namespace precisioncbct::exporter {

/// 把重建体数据写成「逐切片 16 位 PNG + 一个 JSON 头文件」。
///
/// 不引入 DICOM/NIfTI 依赖（那需要新增第三方库），而 PNG 切片序列可以直接被
/// ImageJ / MicroDicom / 3D Slicer 等工具拖进去看；JSON 头里写明
/// 体素间距、原点与灰度量化方式，便于脚本复算。
///
/// 灰度映射：以 [windowMin, windowMax] 线性映射到 0..65535，
/// 默认取 [0, 99.5 百分位]（避开重建边缘的高值伪影）。
/// 文件名：<dir>/slice_0000.png ...（z 从小到大）
bool writeVolumeSlices(const simulation::ReconstructedVolume& volume,
                       const std::string& directory,
                       double windowMin,
                       double windowMax,
                       std::string* error);

/// 便捷重载：自动按 0.5%~99.5% 百分位确定窗宽窗位
bool writeVolumeSlices(const simulation::ReconstructedVolume& volume,
                       const std::string& directory,
                       std::string* error);

} // namespace precisioncbct::exporter
