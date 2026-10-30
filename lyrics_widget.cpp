#include <cmath>
#include <QPainter>
#include <QTextOption>
#include <QFontMetrics>
#include <QShowEvent>
#include <QHideEvent>
#include <QWheelEvent>
#include "lyrics_widget.h"

constexpr qint64 kLyricPredictionOffsetMs = 250;

constexpr double kScrollSmoothingFactor = 0.1;

constexpr int kAnimationFrameInterval = 16;

lyrics_widget::lyrics_widget(QWidget* parent) : QWidget(parent)
{
    font_normal_ = font();
    font_normal_.setPointSize(10);

    font_active_ = font();
    font_active_.setPointSize(14);
    font_active_.setBold(true);

    color_normal_ = QColor("#64758D");
    color_active_ = QColor("#1478E8");

    animation_timer_ = new QTimer(this);
    animation_timer_->setInterval(kAnimationFrameInterval);
    connect(animation_timer_, &QTimer::timeout, this, &lyrics_widget::on_animation_timer);

    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

lyrics_widget::~lyrics_widget() = default;

void lyrics_widget::set_lyrics(const QList<LyricLine>& lyrics)
{
    lyrics_ = lyrics;
    current_index_ = -1;
    target_scroll_y_ = 0;

    current_scroll_y_ = height() / 2.0;

    layout_dirty_ = true;
    setAccessibleDescription({});
    if (lyrics_.isEmpty()) animation_timer_->stop();
    else if (isVisible()) animation_timer_->start();
    update();
}

void lyrics_widget::clear()
{
    set_lyrics({});
}

void lyrics_widget::set_current_time(qint64 time_ms)
{

    if (lyrics_.isEmpty())
    {
        return;
    }

    int new_index = get_line_at_time(time_ms + kLyricPredictionOffsetMs);
    if (new_index != current_index_)
    {
        current_index_ = new_index;
        setAccessibleDescription(current_index_ >= 0 ? lyrics_[current_index_].text : QString());
        if (isVisible()) animation_timer_->start();
        update();
    }
}

int lyrics_widget::get_line_at_time(qint64 time_ms) const
{
    if (lyrics_.isEmpty())
    {
        return -1;
    }

    for (int i = 0; i < lyrics_.size(); ++i)
    {
        if (time_ms >= lyrics_[i].timestamp_ms)
        {
            if (i == lyrics_.size() - 1 || time_ms < lyrics_[i + 1].timestamp_ms)
            {
                return i;
            }
        }
    }
    return -1;
}

void lyrics_widget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    const bool reading = height() > 180;
    font_normal_.setPointSize(reading ? 12 : 10);
    font_active_.setPointSize(reading ? 15 : 12);
    line_spacing_ = reading ? 16 : 8;
    layout_dirty_ = true;
    if (isVisible() && !lyrics_.isEmpty()) animation_timer_->start();
}

void lyrics_widget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!lyrics_.isEmpty()) animation_timer_->start();
}

void lyrics_widget::hideEvent(QHideEvent* event)
{
    animation_timer_->stop();
    QWidget::hideEvent(event);
}

void lyrics_widget::wheelEvent(QWheelEvent* event)
{
    if (line_rects_.isEmpty()) return;
    animation_timer_->stop();
    current_scroll_y_ = qBound(height() / 2.0 - line_rects_.last().bottom(),
                              current_scroll_y_ + event->angleDelta().y() / 2.0, height() / 2.0);
    target_scroll_y_ = current_scroll_y_;
    update();
    event->accept();
}

void lyrics_widget::update_layout()
{
    if (width() <= 0)
    {
        return;
    }

    line_rects_.clear();
    line_rects_.reserve(lyrics_.size());

    QFontMetrics fm(font_active_);
    int width_padding = 20;
    int max_width = width() - (width_padding * 2);

    double current_y = vertical_padding_;

    for (const auto& line : lyrics_)
    {
        QRect rect = fm.boundingRect(0, 0, max_width, 0, Qt::TextWordWrap | Qt::AlignCenter, line.text);

        line_rects_.append(QRectF(0, current_y, max_width, rect.height()));
        current_y += rect.height() + line_spacing_;
    }

    layout_dirty_ = false;
}

void lyrics_widget::on_animation_timer()
{
    if (layout_dirty_) update_layout();
    if (lyrics_.isEmpty() || line_rects_.isEmpty())
    {
        animation_timer_->stop();
        return;
    }

    if (current_index_ >= 0 && current_index_ < line_rects_.size())
    {
        QRectF target_line = line_rects_[current_index_];
        double line_center_y = target_line.y() + (target_line.height() / 2.0);

        target_scroll_y_ = (height() / 2.0) - line_center_y;
    }
    else
    {
        target_scroll_y_ = height() / 2.0;
    }

    if (std::abs(target_scroll_y_ - current_scroll_y_) > 0.5)
    {
        current_scroll_y_ += (target_scroll_y_ - current_scroll_y_) * kScrollSmoothingFactor;
        update();
    }
    else animation_timer_->stop();
}

void lyrics_widget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setClipRect(rect());

    if (lyrics_.isEmpty())
    {
        painter.setFont(font_normal_);
        painter.setPen(color_normal_);
        painter.drawText(rect(), Qt::AlignCenter, "暂无歌词");
        return;
    }

    if (layout_dirty_)
    {
        update_layout();
    }

    int width_padding = 20;

    painter.translate(width_padding, current_scroll_y_);

    for (int i = 0; i < lyrics_.size(); ++i)
    {
        const QRectF& line_rect = line_rects_[i];

        double screen_y = line_rect.y() + current_scroll_y_;
        if (screen_y > height() || screen_y + line_rect.height() < 0)
        {
            continue;
        }

        bool is_active = (i == current_index_);

        if (is_active)
        {
            painter.setFont(font_active_);
            painter.setPen(color_active_);
        }
        else
        {
            painter.setFont(font_normal_);

            painter.setPen(color_normal_);
        }

        painter.drawText(line_rect, Qt::TextWordWrap | Qt::AlignCenter, lyrics_[i].text);
    }
}
