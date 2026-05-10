#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStyleOption>

#include "scrolling_text_label.h"

namespace
{
constexpr int kScrollIntervalMs = 45;
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
        update();
    });
}

void scrolling_text_label::setText(const QString& text)
{
    if (QLabel::text() == text)
    {
        return;
    }

    QLabel::setText(text);
    scroll_offset_px_ = 0;
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

void scrolling_text_label::refresh_scroll_state()
{
    const QFontMetrics metrics(font());
    const bool should_scroll = metrics.horizontalAdvance(text()) > contentsRect().width();

    if (should_scroll)
    {
        if (!scroll_timer_.isActive())
        {
            scroll_timer_.start(kScrollIntervalMs);
        }
        return;
    }

    scroll_timer_.stop();
    scroll_offset_px_ = 0;
}
