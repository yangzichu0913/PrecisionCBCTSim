#pragma once

#include "simulation/ScanParameters.h"

namespace precisioncbct::simulation {

/// 机架类型：三种典型 CT 形态，几何/能谱默认值与三维外观都随之切换。
///
/// - Medical   ：医用 CBCT，患者卧位，环形机架 + 检查床绕患者旋转；
/// - Industrial：工业微焦点 CT，源与探测器固定在门架上，样品放在转台上自转；
/// - CArm      ：C 型臂 CBCT（术中），C 形弧臂连接球管与平板，绕患者旋转。
enum class ScannerMode
{
    Medical,
    Industrial,
    CArm,
};

/// 界面/命令行用的稳定标识："medical" / "industrial" / "carm"
const char* scannerModeId(ScannerMode mode);

/// 解析标识（大小写不敏感）；无法识别时返回 false 且不改 *out
bool parseScannerMode(const char* text, ScannerMode* out);

/// 中文显示名（界面下拉框）
const char* scannerModeLabel(ScannerMode mode);

/// 一句话说明（下拉框提示 + 状态栏）
const char* scannerModeDescription(ScannerMode mode);

/// 该机架的推荐几何参数
ScanParameters scannerModeGeometry(ScannerMode mode);

/// 该机架的推荐能谱/探测器设置
SpectralSettings scannerModeSpectral(ScannerMode mode);

/// 该机架推荐的体素尺寸（mm），仅作为界面提示
double scannerModeVoxelSizeMm(ScannerMode mode);

/// 该机架下"谁在转"：工业 CT 是样品在转台上转，其余是机架转
bool scannerModeRotatesSample(ScannerMode mode);

} // namespace precisioncbct::simulation
