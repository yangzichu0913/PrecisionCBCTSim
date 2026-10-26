#pragma once

#include <QAbstractScrollArea>
#include <QImage>
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;

namespace precisioncbct::app {

/// 图像画布：整块深色背景，图像按比例缩放居中，左上角叠加 HUD 文字。
///
/// 单独抽出来是因为 QLabel 承载 pixmap 时无法同时做到「背景铺满 + 图像居中 +
/// 角落叠加信息」，而且会跟随 pixmap 撑大布局。
class ImageCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit ImageCanvas(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void clearImage();
    void setOverlayText(const QString& text);
    void setPlaceholderText(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage m_image;
    QString m_overlay;
    QString m_placeholder;
};

/// 序列胶片条：把整段投影一次性铺开，每条下方是帧号与角度，底部一条横向滚动条。
///
/// 用 QAbstractScrollArea 自绘而不是堆 QWidget：360~720 帧时子控件方案会明显卡顿，
/// 自绘只画可视范围内的帧，也不需要缓存整段缩略图（512² × 720 ≈ 190 MB）。
class FrameStrip : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit FrameStrip(QWidget* parent = nullptr);

    void setFrames(const QVector<QImage>& frames, const QVector<double>& anglesDeg);
    void clearFrames();

    int currentIndex() const { return m_current; }
    void setCurrentIndex(int index);

    int frameCount() const { return m_frames.size(); }

signals:
    /// 用户点击/方向键切换了某一帧（0 起）
    void frameActivated(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct Cell
    {
        QRect rect;      ///< 缩略图区域（不含帧号）
        QString caption; ///< 「12 · 33.0°」
    };

    void rebuildLayout() const;
    void ensureVisibleIndex(int index);
    int indexAt(const QPoint& pos) const;

    QVector<QImage> m_frames;
    QVector<double> m_anglesDeg;
    mutable QVector<Cell> m_cells;
    mutable QSize m_layoutSize;
    int m_current = -1;
};

/// DR 预览面板。
///
/// 两种显示模式：
/// - 单帧（DR 图）：setDisplayImage()，显示大图；
/// - 序列：setFrames()，整段帧以胶片条铺开（可横向滚动，点选某一帧）。
/// 物理/几何摘要收进可折叠的「详情」，避免长期占掉图像高度。
class DRPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DRPreviewWidget(QWidget* parent = nullptr);

    void setDisplayImage(const QImage& image, const QString& summary);
    void setFrames(const QVector<QImage>& frames, const QVector<double>& anglesDeg,
                   const QString& summary);
    void showPlaceholder(const QString& text);

    /// 跳到指定帧（会触发 frameChanged）；越界时忽略
    void setCurrentFrame(int index);

    /// 当前正在显示的帧序号；无帧时返回 -1
    int currentFrame() const;
    int frameCount() const { return m_frames.size(); }

signals:
    /// 用户点选/切换帧（0 起）
    void frameChanged(int index);

private:
    void syncDetails();

    ImageCanvas* m_canvas = nullptr;
    FrameStrip* m_strip = nullptr;
    QPushButton* m_detailsToggle = nullptr;
    QLabel* m_summaryLabel = nullptr;

    QImage m_image;
    QVector<QImage> m_frames;
    QVector<double> m_anglesDeg;
    QString m_baseSummary;
    QString m_placeholder = QStringLiteral("尚未生成 DR 图");
};

} // namespace precisioncbct::app
