#pragma once

class QApplication;

namespace precisioncbct::app {

/// 应用统一的深色主题（Fusion 样式 + 调色板 + 样式表）。
///
/// 目的：让控件有统一的层级、间距与强调色，观感对齐 VS Code（13px 正文、
/// 24px 左右的控件高度、1px 分隔线、低饱和强调色），只作用于 Qt 控件；
/// VTK 视图自己画背景，不受样式表影响。
void applyTheme(QApplication& app);

/// 主题配色（冷灰蓝，稍带一点蓝调）供需要自绘的控件复用，避免各处硬编码颜色
namespace theme {
constexpr const char* kBackground = "#1b1f24";    ///< 主编辑区 / 3D 视口底色
constexpr const char* kSurface = "#22272e";       ///< 面板、菜单栏、工具栏
constexpr const char* kSurfaceRaised = "#2a3038"; ///< 次级表面
constexpr const char* kInput = "#1b1f24";         ///< 输入控件底色（比面板更暗）
constexpr const char* kCanvas = "#101419";        ///< 图像画布（最深，衬托图像）
constexpr const char* kBorder = "#363d47";
constexpr const char* kText = "#d7dde5";
constexpr const char* kTextMuted = "#8b96a3";
constexpr const char* kAccent = "#54a8ff";        ///< 焦点框 / 选中描边
constexpr const char* kAccentDim = "#2f6da8";     ///< 选中底色 / 主按钮
constexpr const char* kWarning = "#e8b339";
constexpr const char* kDanger = "#e5534b";
constexpr const char* kSuccess = "#3fb950";
} // namespace theme

} // namespace precisioncbct::app
