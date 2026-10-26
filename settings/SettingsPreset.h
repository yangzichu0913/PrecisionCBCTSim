#pragma once

#include <string>

#include "simulation/ScanParameters.h"

namespace precisioncbct::settings {

/// 一次仿真所需的完整参数快照（用于「保存/加载参数预设」）。
///
/// 只描述参数，不保存任何几何/体素数据：加载预设后仍需打开同一个模型文件，
/// 才能得到可复现的结果。
struct SimulationPreset
{
    simulation::ScanParameters scan;
    simulation::SpectralSettings spectral;
    double shellThicknessMm = 0.0; ///< 皮质骨外壳厚度（0 = 不做材料分层）
    double voxelSizeMm = 1.0;      ///< 体素尺寸，仅作记录（加载后需自行重新体素化）
    std::string scannerMode = "medical"; ///< 机架类型（medical / industrial / carm）
};

/// 序列化为 JSON 文本（2 空格缩进）
std::string toJson(const SimulationPreset& preset);

/// 从 JSON 文本解析；未知字段忽略，缺失字段沿用 preset 的原值。
/// 解析失败或关键参数越界时返回 false 并填写 error。
bool fromJson(const std::string& text, SimulationPreset* preset, std::string* error);

/// 写入/读取 UTF-8 JSON 文件
bool saveToFile(const std::string& path, const SimulationPreset& preset, std::string* error);
bool loadFromFile(const std::string& path, SimulationPreset* preset, std::string* error);

} // namespace precisioncbct::settings
