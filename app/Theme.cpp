#include "app/Theme.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>

namespace precisioncbct::app {
namespace {

/// 深色样式表。尺寸密度对齐 VS Code（13px 正文 / 紧凑内边距），
/// 配色沿用冷灰蓝（#1b1f24 系），比纯灰更有层次、也不发黄。
///
/// 原则：
/// - 正文 13px、次要文字 12px，控件高度 24px 上下：和 VS Code 的密度一致，
///   既不像系统默认那样"散"，也不会挤成一片；
/// - 边距克制：菜单项 4px、工具栏按钮 4px、分组框 8~10px，不做大片留白；
/// - 强调色（#54a8ff）只用在焦点框、选中项与主操作上，其余保持低对比。
const char* kStyleSheet = R"(
QWidget { font-size: 13px; }
QWidget#CentralHost { background: #1b1f24; }

QMenuBar {
    background: #22272e;
    color: #d7dde5;
    border-bottom: 1px solid #2a3038;
    padding: 0px 4px;
}
QMenuBar::item { padding: 3px 9px; margin: 1px 0; border-radius: 3px; }
QMenuBar::item:selected { background: #2c333c; }
QMenuBar::item:pressed { background: #333a44; }

QMenu {
    background: #22272e;
    color: #d7dde5;
    border: 1px solid #363d47;
    border-radius: 4px;
    padding: 4px;
}
QMenu::item { padding: 4px 22px 4px 10px; border-radius: 3px; }
QMenu::item:selected { background: #2f6da8; color: #ffffff; }
QMenu::item:disabled { color: #6b7480; }
QMenu::separator { height: 1px; background: #363d47; margin: 4px 6px; }

QToolBar {
    background: #22272e;
    border: none;
    border-bottom: 1px solid #2a3038;
    padding: 2px 4px;
    spacing: 0px;
}
QToolBar::separator { width: 1px; background: #363d47; margin: 4px 5px; }

QToolButton {
    background: transparent;
    color: #d7dde5;
    border: 1px solid transparent;
    border-radius: 3px;
    padding: 3px 7px;
}
QToolButton:hover { background: #2c333c; }
QToolButton:pressed { background: #333a44; }
QToolButton:checked { background: #333a44; color: #ffffff; }
QToolButton:disabled { color: #6b7480; }

QDockWidget { color: #d7dde5; }
QDockWidget::title {
    background: #22272e;
    padding: 4px 8px;
    border-bottom: 1px solid #2a3038;
    font-size: 12px;
    font-weight: 600;
    color: #8b96a3;
}
QDockWidget::close-button, QDockWidget::float-button {
    background: transparent; border: none; padding: 1px;
}
QDockWidget::close-button:hover, QDockWidget::float-button:hover { background: #363d47; }

QScrollArea { background: transparent; border: none; }
QScrollBar:vertical { background: transparent; width: 12px; margin: 0px; }
QScrollBar::handle:vertical {
    background: #3d4550; min-height: 24px; border-radius: 6px; margin: 2px;
}
QScrollBar::handle:vertical:hover { background: #4d5763; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 0px; }
QScrollBar::handle:horizontal {
    background: #3d4550; min-width: 24px; border-radius: 6px; margin: 2px;
}
QScrollBar::handle:horizontal:hover { background: #4d5763; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }

QGroupBox {
    background: #22272e;
    border: 1px solid #2f363f;
    border-radius: 4px;
    margin-top: 9px;
    padding: 6px 8px 2px 8px;
    font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 8px;
    padding: 0px 4px;
    color: #8b96a3;
    font-size: 12px;
    background: #22272e;
}

QLabel { color: #d7dde5; background: transparent; }
QLabel#Hint { color: #8b96a3; font-size: 12px; }
QLabel#SectionHint { color: #8b96a3; font-size: 12px; font-weight: 400; }
QLabel#DerivedInfo { color: #8b96a3; font-size: 12px; font-weight: 400; }
QLabel#ValidationWarning { color: #e5534b; font-size: 12px; font-weight: 400; }
QLabel#Badge {
    color: #d7dde5;
    background: #2a3038;
    border: 1px solid #363d47;
    border-radius: 3px;
    padding: 1px 6px;
    font-size: 12px;
}

QSpinBox, QDoubleSpinBox, QLineEdit, QComboBox {
    background: #1b1f24;
    color: #d7dde5;
    border: 1px solid #363d47;
    border-radius: 3px;
    padding: 3px 6px;
    min-height: 18px;
    selection-background-color: #2f6da8;
}
QSpinBox:focus, QDoubleSpinBox:focus, QLineEdit:focus, QComboBox:focus {
    border-color: #54a8ff;
}
QSpinBox:disabled, QDoubleSpinBox:disabled, QLineEdit:disabled, QComboBox:disabled {
    color: #6b7480; background: #1d2127;
}
QSpinBox::up-button, QDoubleSpinBox::up-button,
QSpinBox::down-button, QDoubleSpinBox::down-button {
    background: #2a3038; border: none; width: 14px;
}
QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover { background: #46505d; }
QComboBox::drop-down { border: none; width: 16px; }
QComboBox QAbstractItemView {
    background: #22272e; color: #d7dde5; border: 1px solid #363d47;
    selection-background-color: #2f6da8;
    outline: 0;
}

QPushButton {
    background: #2a3038;
    color: #d7dde5;
    border: 1px solid #2a3038;
    border-radius: 3px;
    padding: 4px 12px;
    min-height: 18px;
}
QPushButton:hover { background: #333a44; border-color: #333a44; }
QPushButton:pressed { background: #262c34; }
QPushButton:disabled { color: #6b7480; background: #1d2127; border-color: #1d2127; }
QPushButton#Primary {
    background: #2f6da8; border-color: #2f6da8; color: #ffffff;
}
QPushButton#Primary:hover { background: #3b83c6; border-color: #3b83c6; }
QPushButton#Primary:pressed { background: #2a6199; }
QPushButton#Ghost {
    background: transparent; border-color: #363d47; color: #8b96a3;
}
QPushButton#Ghost:hover { background: #2c333c; color: #d7dde5; }

QCheckBox { color: #d7dde5; spacing: 6px; }
QCheckBox::indicator {
    width: 15px; height: 15px; border-radius: 3px;
    border: 1px solid #46505d; background: #1b1f24;
}
QCheckBox::indicator:hover { border-color: #5b6675; }
QCheckBox::indicator:checked { background: #54a8ff; border-color: #54a8ff; }
QCheckBox::indicator:disabled { background: #1d2127; border-color: #333a44; }

QSlider::groove:horizontal { height: 4px; background: #333a44; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #54a8ff; border-radius: 2px; }
QSlider::handle:horizontal {
    background: #d7dde5; width: 12px; margin: -5px 0; border-radius: 6px;
}
QSlider::handle:horizontal:hover { background: #ffffff; }

QProgressBar {
    background: #2a3038;
    border: 1px solid #363d47;
    border-radius: 3px;
    text-align: center;
    color: #d7dde5;
    font-size: 12px;
    min-height: 14px;
    max-height: 16px;
}
QProgressBar::chunk { background: #2f6da8; border-radius: 2px; }

QStatusBar { background: #1b1f24; border-top: 1px solid #2a3038; color: #8b96a3; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: #8b96a3; padding: 0 6px; font-size: 12px; }

QTabWidget::pane { border: 1px solid #2f2f2f; top: -1px; }
QTabBar::tab {
    background: transparent; color: #8b96a3; padding: 4px 12px;
    border-bottom: 1px solid transparent;
}
QTabBar::tab:selected { color: #d7dde5; border-bottom: 1px solid #54a8ff; }
QTabBar::tab:hover { color: #d7dde5; }

QDialog, QMessageBox { background: #1b1f24; }
QDialog QLabel, QMessageBox QLabel { color: #d7dde5; }
QDialog QGroupBox { background: #22272e; }

QToolTip {
    background: #22272e; color: #d7dde5;
    border: 1px solid #333a44; border-radius: 3px; padding: 4px 6px;
    font-size: 12px;
}

QMainWindow::separator { background: #2a3038; width: 4px; height: 4px; }
QMainWindow::separator:hover { background: #54a8ff; }

QSplitter::handle { background: #2a3038; }
QSplitter::handle:horizontal { width: 4px; }
QSplitter::handle:vertical { height: 4px; }
)";

/// 调色板：让没有被样式表覆盖到的原生绘制（如文件对话框）也保持深色
QPalette buildPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(theme::kBackground));
    palette.setColor(QPalette::WindowText, QColor(theme::kText));
    palette.setColor(QPalette::Base, QColor(theme::kSurface));
    palette.setColor(QPalette::AlternateBase, QColor(theme::kSurfaceRaised));
    palette.setColor(QPalette::ToolTipBase, QColor(theme::kSurface));
    palette.setColor(QPalette::ToolTipText, QColor(theme::kText));
    palette.setColor(QPalette::Text, QColor(theme::kText));
    palette.setColor(QPalette::Button, QColor(theme::kSurfaceRaised));
    palette.setColor(QPalette::ButtonText, QColor(theme::kText));
    palette.setColor(QPalette::BrightText, QColor(theme::kDanger));
    palette.setColor(QPalette::Link, QColor(theme::kAccent));
    palette.setColor(QPalette::Highlight, QColor(theme::kAccentDim));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::PlaceholderText, QColor(theme::kTextMuted));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#6b7480"));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#6b7480"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#6b7480"));
    return palette;
}

} // namespace

void applyTheme(QApplication& app)
{
    // Fusion 是唯一在各平台都能被样式表完全控制的样式
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setPalette(buildPalette());

    QFont font = app.font();
    // 13px（VS Code 正文字号）：Windows 96 dpi 下 13px ≈ 9.75pt；
    // 用磅值而不是像素值，高 DPI 缩放时字体会按比例放大而不是变成蚂蚁字
    font.setPointSizeF(9.75);
    font.setFamily(font.family());
    app.setFont(font);

    app.setStyleSheet(QString::fromUtf8(kStyleSheet));
}

} // namespace precisioncbct::app
