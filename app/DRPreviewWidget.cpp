#include "app/DRPreviewWidget.h"

#include "app/Theme.h"

#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

namespace precisioncbct::app {
namespace {

constexpr int kStripGap = 8;        ///< 相邻缩略图间距
constexpr int kStripMargin = 6;     ///< 胶片条内边距
constexpr int kCaptionHeight = 16;  ///< 帧号/角度文字高度
constexpr int kMinThumb = 56;       ///< 缩略图最小边长（窗口很矮时仍可辨认）
constexpr int kMaxThumb = 260;      ///< 缩略图最大边长，避免单帧占满整块

} // namespace

// ---------------------------------------------------------------- ImageCanvas

ImageCanvas::ImageCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(200, 120);
    // Ignored 让画布自己吃掉多余空间，而不是被图像 sizeHint 撑大
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    setAutoFillBackground(false);
}

void ImageCanvas::setImage(const QImage& image)
{
    m_image = image;
    update();
}

void ImageCanvas::clearImage()
{
    m_image = QImage();
    update();
}

void ImageCanvas::setOverlayText(const QString& text)
{
    m_overlay = text;
    update();
}

void ImageCanvas::setPlaceholderText(const QString& text)
{
    m_placeholder = text;
    update();
}

void ImageCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    // 圆角深色底：让预览区像一块独立的"读片屏"，而不是空白控件
    const QRectF panel = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath clip;
    clip.addRoundedRect(panel, 6, 6);
    painter.fillPath(clip, QColor(theme::kCanvas));

    if (m_image.isNull())
    {
        painter.setPen(QColor(theme::kTextMuted));
        painter.drawText(rect(), Qt::AlignCenter, m_placeholder);
        return;
    }

    // 保持宽高比缩放到可用区域（留 8px 边距），然后居中绘制
    const QRect content = rect().adjusted(8, 8, -8, -8);
    if (content.width() <= 0 || content.height() <= 0)
    {
        return;
    }
    const QSize scaled = m_image.size().scaled(content.size(), Qt::KeepAspectRatio);
    const QRect target(QPoint(content.center().x() - scaled.width() / 2,
                              content.center().y() - scaled.height() / 2),
                       scaled);

    painter.save();
    painter.setClipPath(clip);
    painter.drawImage(target, m_image);
    painter.restore();

    // 图像细边框：让亮色图像与深色背景之间有清晰分界
    painter.setPen(QPen(QColor(255, 255, 255, 30), 1));
    painter.drawRect(target.adjusted(0, 0, -1, -1));

    if (!m_overlay.isEmpty())
    {
        QFont font = painter.font();
        font.setPointSizeF(std::max(7.5, font.pointSizeF() - 1.0));
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);

        const QFontMetrics metrics(font);
        const QRect textRect = metrics.boundingRect(m_overlay).adjusted(-8, -4, 8, 4);
        const QRect badge(target.left() + 6, target.top() + 6, textRect.width(),
                          textRect.height());
        QPainterPath badgePath;
        badgePath.addRoundedRect(QRectF(badge), 4, 4);
        painter.fillPath(badgePath, QColor(10, 14, 19, 200));
        painter.setPen(QColor(theme::kText));
        painter.drawText(badge, Qt::AlignCenter, m_overlay);
    }
}

// ------------------------------------------------------------------ FrameStrip

FrameStrip::FrameStrip(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(120);
}

void FrameStrip::setFrames(const QVector<QImage>& frames, const QVector<double>& anglesDeg)
{
    m_frames = frames;
    m_anglesDeg = anglesDeg;
    m_current = m_frames.isEmpty() ? -1 : 0;
    m_cells.clear();
    rebuildLayout();
    viewport()->update();
}

void FrameStrip::clearFrames()
{
    m_frames.clear();
    m_anglesDeg.clear();
    m_cells.clear();
    m_current = -1;
    rebuildLayout();
    viewport()->update();
}

void FrameStrip::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_frames.size() || index == m_current)
    {
        return;
    }
    m_current = index;
    ensureVisibleIndex(index);
    viewport()->update();
}

void FrameStrip::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    m_cells.clear();
    rebuildLayout();
}

void FrameStrip::rebuildLayout() const
{
    m_cells.clear();
    if (m_frames.isEmpty())
    {
        m_layoutSize = QSize();
        return;
    }

    const int available = viewport()->height() - kCaptionHeight - 2 * kStripMargin;
    const int thumbHeight = std::clamp(available, kMinThumb, kMaxThumb);
    // 缩略图被封顶时整行居中，避免上方留白显得像布局出错
    const int rowTop =
        std::max(kStripMargin, (viewport()->height() - thumbHeight - 2 - kCaptionHeight) / 2);

    QFont captionFont = font();
    captionFont.setPointSizeF(std::max(7.5, captionFont.pointSizeF() - 1.5));

    int x = kStripMargin;
    m_cells.reserve(m_frames.size());
    for (int i = 0; i < m_frames.size(); ++i)
    {
        const QImage& image = m_frames.at(i);
        // DR 图基本是方形，但仍按实际宽高比算，非方形探测器也不会被拉伸
        const double aspect = image.height() > 0
                                  ? static_cast<double>(image.width()) / image.height()
                                  : 1.0;
        const int thumbWidth =
            std::clamp(static_cast<int>(std::lround(thumbHeight * aspect)), kMinThumb,
                       kMaxThumb * 2);
        Cell cell;
        cell.rect = QRect(x, rowTop, thumbWidth, thumbHeight);
        const double angle = i < m_anglesDeg.size() ? m_anglesDeg.at(i) : 0.0;
        cell.caption = QStringLiteral("%1 · %2°").arg(i + 1).arg(angle, 0, 'f', 0);
        m_cells.append(cell);
        x += thumbWidth + kStripGap;
    }

    const int totalWidth = x - kStripGap + kStripMargin;
    m_layoutSize = QSize(totalWidth, viewport()->height());
    horizontalScrollBar()->setRange(0, std::max(0, totalWidth - viewport()->width()));
    horizontalScrollBar()->setPageStep(viewport()->width());
    horizontalScrollBar()->setSingleStep(std::max(24, thumbHeight / 2));
}

void FrameStrip::paintEvent(QPaintEvent*)
{
    QPainter painter(viewport());
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    const QRectF panel = QRectF(viewport()->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath clip;
    clip.addRoundedRect(panel, 6, 6);
    painter.fillPath(clip, QColor(theme::kCanvas));

    if (m_frames.isEmpty())
    {
        painter.setPen(QColor(theme::kTextMuted));
        painter.drawText(viewport()->rect(), Qt::AlignCenter, QStringLiteral("序列为空"));
        return;
    }

    if (m_cells.isEmpty() || m_layoutSize.height() != viewport()->height())
    {
        rebuildLayout();
    }

    const int offset = horizontalScrollBar()->value();
    painter.save();
    painter.setClipPath(clip);

    QFont captionFont = painter.font();
    captionFont.setPointSizeF(std::max(7.5, captionFont.pointSizeF() - 1.5));
    painter.setFont(captionFont);

    for (int i = 0; i < m_cells.size(); ++i)
    {
        const Cell& cell = m_cells.at(i);
        QRect target = cell.rect.translated(-offset, 0);
        if (target.right() < 0 || target.left() > viewport()->width())
        {
            continue; // 只画可视范围内的帧
        }

        const bool selected = i == m_current;
        painter.drawImage(target, m_frames.at(i));

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(selected ? theme::kAccent : theme::kBorder),
                            selected ? 2 : 1));
        painter.drawRect(target.adjusted(0, 0, -1, -1));

        const QRect captionRect(target.left(), target.bottom() + 2, target.width(),
                                kCaptionHeight);
        painter.setPen(QColor(selected ? theme::kAccent : theme::kTextMuted));
        painter.drawText(captionRect, Qt::AlignHCenter | Qt::AlignTop, cell.caption);
    }

    painter.restore();
}

void FrameStrip::mousePressEvent(QMouseEvent* event)
{
    const int index = indexAt(event->pos());
    if (index >= 0 && index != m_current)
    {
        m_current = index;
        viewport()->update();
        emit frameActivated(index);
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void FrameStrip::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
    {
        const int step = event->key() == Qt::Key_Left ? -1 : 1;
        const int last = static_cast<int>(m_frames.size()) - 1;
        const int next = std::clamp(m_current + step, 0, last);
        if (next != m_current)
        {
            setCurrentIndex(next);
            emit frameActivated(next);
        }
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void FrameStrip::wheelEvent(QWheelEvent* event)
{
    // 胶片条是横向排列的，普通滚轮也应该能横向翻，不必按 Shift
    const int delta = event->angleDelta().y() != 0 ? event->angleDelta().y()
                                                   : event->angleDelta().x();
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta);
    event->accept();
}

void FrameStrip::ensureVisibleIndex(int index)
{
    if (m_cells.isEmpty() || index < 0 || index >= m_cells.size())
    {
        return;
    }
    const QRect rect = m_cells.at(index).rect;
    QScrollBar* bar = horizontalScrollBar();
    const int viewLeft = bar->value();
    const int viewRight = viewLeft + viewport()->width();
    if (rect.left() < viewLeft)
    {
        bar->setValue(rect.left() - kStripGap);
    }
    else if (rect.right() > viewRight)
    {
        bar->setValue(rect.right() - viewport()->width() + kStripGap);
    }
}

int FrameStrip::indexAt(const QPoint& pos) const
{
    if (m_cells.isEmpty())
    {
        return -1;
    }
    const QPoint content(pos.x() + horizontalScrollBar()->value(), pos.y());
    for (int i = 0; i < m_cells.size(); ++i)
    {
        if (m_cells.at(i).rect.contains(content))
        {
            return i;
        }
    }
    return -1;
}

// ------------------------------------------------------------ DRPreviewWidget

DRPreviewWidget::DRPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    m_canvas = new ImageCanvas(this);
    m_canvas->setPlaceholderText(m_placeholder);

    m_strip = new FrameStrip(this);
    m_strip->setVisible(false);
    connect(m_strip, &FrameStrip::frameActivated, this, [this](int index) {
        emit frameChanged(index);
    });

    // 详情：默认收起，只留一个按钮，把高度让给图像
    m_detailsToggle = new QPushButton(QStringLiteral("详情"), this);
    m_detailsToggle->setCheckable(true);
    m_detailsToggle->setFlat(true);
    m_detailsToggle->setCursor(Qt::PointingHandCursor);
    m_detailsToggle->setStyleSheet(
        QStringLiteral("QPushButton { color: #8b96a3; text-align: left; padding: 2px 2px; "
                       "border: none; background: transparent; }"
                       "QPushButton:hover { color: #d7dde5; background: transparent; }"
                       "QPushButton:checked { color: #d7dde5; background: transparent; }"));

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setObjectName(QStringLiteral("Hint"));
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setTextFormat(Qt::PlainText);
    m_summaryLabel->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_summaryLabel->setVisible(false);
    m_summaryLabel->setContentsMargins(2, 0, 2, 2);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 4);
    layout->setSpacing(4);
    layout->addWidget(m_canvas, 1);
    layout->addWidget(m_strip, 1);
    layout->addWidget(m_detailsToggle);
    layout->addWidget(m_summaryLabel);

    connect(m_detailsToggle, &QPushButton::toggled, this, [this](bool checked) {
        m_summaryLabel->setVisible(checked && !m_baseSummary.isEmpty());
        m_detailsToggle->setText(checked ? QStringLiteral("收起详情")
                                        : QStringLiteral("详情"));
    });

    showPlaceholder(m_placeholder);
}

void DRPreviewWidget::setDisplayImage(const QImage& image, const QString& summary)
{
    m_frames.clear();
    m_anglesDeg.clear();
    m_baseSummary = summary;

    m_image = image;
    m_canvas->setImage(image);
    m_canvas->setOverlayText(QString());
    m_strip->clearFrames();
    m_strip->setVisible(false);
    m_canvas->setVisible(true);

    m_summaryLabel->setText(summary);
    syncDetails();
}

void DRPreviewWidget::setFrames(const QVector<QImage>& frames, const QVector<double>& anglesDeg,
                                const QString& summary)
{
    if (frames.isEmpty())
    {
        showPlaceholder(QStringLiteral("序列为空"));
        return;
    }

    m_frames = frames;
    m_anglesDeg = anglesDeg;
    m_baseSummary = summary;
    m_image = QImage();

    // 多帧时整段铺开：一眼看到全部投影，比一帧一帧拖滑块更接近"读片"的习惯
    m_canvas->clearImage();
    m_canvas->setVisible(false);
    m_strip->setVisible(true);
    m_strip->setFrames(frames, anglesDeg);

    m_summaryLabel->setText(summary);
    syncDetails();

    // 让三维视图里的射线源/探测器立刻对齐到第 1 帧
    emit frameChanged(0);
}

void DRPreviewWidget::showPlaceholder(const QString& text)
{
    m_placeholder = text;
    m_image = QImage();
    m_frames.clear();
    m_anglesDeg.clear();
    m_strip->clearFrames();
    m_strip->setVisible(false);
    m_canvas->setVisible(true);
    m_canvas->clearImage();
    m_canvas->setPlaceholderText(text);
    m_canvas->setOverlayText(QString());
    m_detailsToggle->setVisible(false);
    m_summaryLabel->clear();
    m_summaryLabel->setVisible(false);
}

int DRPreviewWidget::currentFrame() const
{
    if (m_frames.isEmpty())
    {
        return -1;
    }
    return m_strip->isVisible() ? m_strip->currentIndex() : 0;
}

void DRPreviewWidget::setCurrentFrame(int index)
{
    if (m_frames.isEmpty() || index < 0 || index >= m_frames.size())
    {
        return;
    }
    if (!m_strip->isVisible())
    {
        return;
    }
    const int previous = m_strip->currentIndex();
    m_strip->setCurrentIndex(index);
    if (m_strip->currentIndex() != previous)
    {
        emit frameChanged(m_strip->currentIndex());
    }
}

void DRPreviewWidget::syncDetails()
{
    const bool hasSummary = !m_baseSummary.isEmpty();
    m_detailsToggle->setVisible(hasSummary);
    m_summaryLabel->setVisible(hasSummary && m_detailsToggle->isChecked());
}

} // namespace precisioncbct::app
