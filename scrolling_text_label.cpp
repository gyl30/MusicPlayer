#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QEnterEvent>
#include <QStyleOption>

#include "scrolling_text_label.h"

namespace
{
constexpr int kScrollIntervalMs = 30;
constexpr int kIntroDelayMs = 1500;
constexpr int kScrollStepPx = 1;
constexpr int kRepeatGapPx = 36;
}  // namespace

scrolling_text_label::scrolling_text_label(QWidget* parent) : QLabel(parent)
{
    setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    setAttribute(Qt::WA_TranslucentBackground);

    connect(&scroll_timer_, &QTimer::timeout, this, [this] {
        const QFontMetrics metrics(font());
        const int cycle_width = metrics.horizontalAdvance(text()) + kRepeatGapPx;
        if (cycle_width <= 0)
        {
            return;
        }

        scroll_offset_px_ = (scroll_offset_px_ + kScrollStepPx) % cycle_width;
        if (scroll_offset_px_ == 0)
        {
            scroll_timer_.stop();
        }
        update();
    });

    intro_delay_.setSingleShot(true);
    intro_delay_.setInterval(kIntroDelayMs);
    connect(&intro_delay_, &QTimer::timeout, this, [this] {
        if (overflows())
        {
            scroll_timer_.start(kScrollIntervalMs);
        }
    });
}

void scrolling_text_label::setText(const QString& text)
{
    if (QLabel::text() == text)
    {
        return;
    }

    QLabel::setText(text);
    scroll_timer_.stop();
    intro_delay_.stop();
    scroll_offset_px_ = 0;
    intro_pending_ = true;
    refresh_scroll_state();
    update();
}

QSize scrolling_text_label::sizeHint() const
{
    const QFontMetrics metrics(font());
    return QSize(80, metrics.height());
}

void scrolling_text_label::paintEvent(QPaintEvent* event)
{
    (void)event;

    QStyleOption option;
    option.initFrom(this);

    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);

    const QString current_text = text();
    if (current_text.isEmpty())
    {
        return;
    }

    const QRect area = contentsRect();
    const QFontMetrics metrics(font());
    const int text_width = metrics.horizontalAdvance(current_text);

    painter.setFont(font());
    painter.setPen(palette().color(QPalette::WindowText));
    painter.setClipRect(area);

    if (text_width <= area.width())
    {
        painter.drawText(area, alignment(), current_text);
        return;
    }

    const int y = area.top();
    const int x = area.left() - scroll_offset_px_;
    painter.drawText(QRect(x, y, text_width, area.height()), Qt::AlignLeft | Qt::AlignVCenter, current_text);
    painter.drawText(QRect(x + text_width + kRepeatGapPx, y, text_width, area.height()), Qt::AlignLeft | Qt::AlignVCenter, current_text);
}

void scrolling_text_label::resizeEvent(QResizeEvent* event)
{
    QLabel::resizeEvent(event);
    refresh_scroll_state();
}

void scrolling_text_label::showEvent(QShowEvent* event)
{
    QLabel::showEvent(event);
    refresh_scroll_state();
}

void scrolling_text_label::enterEvent(QEnterEvent* event)
{
    QLabel::enterEvent(event);
    if (overflows() && !scroll_timer_.isActive())
    {
        intro_delay_.stop();
        scroll_timer_.start(kScrollIntervalMs);
    }
}

bool scrolling_text_label::overflows() const
{
    return QFontMetrics(font()).horizontalAdvance(text()) > contentsRect().width();
}

void scrolling_text_label::refresh_scroll_state()
{
    if (!overflows())
    {
        scroll_timer_.stop();
        intro_delay_.stop();
        scroll_offset_px_ = 0;
        update();
        return;
    }
    // 文本在布局完成、可见后才能确定是否溢出，此时再开始开场停留。
    if (intro_pending_ && isVisible())
    {
        intro_pending_ = false;
        intro_delay_.start();
    }
}
