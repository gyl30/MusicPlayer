#include <cmath>
#include <QPainter>
#include <QTextOption>
#include <QFontMetrics>
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

    color_normal_ = QColor(128, 128, 128, 180);
    color_active_ = QColor(52, 152, 219, 255);

    animation_timer_ = new QTimer(this);
    animation_timer_->setInterval(kAnimationFrameInterval);
    connect(animation_timer_, &QTimer::timeout, this, &lyrics_widget::on_animation_timer);
    animation_timer_->start();

    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

lyrics_widget::~lyrics_widget() = default;

void lyrics_widget::set_lyrics(const QList<LyricLine>& lyrics)
{
    lyrics_ = lyrics;
    current_index_ = -1;
    current_time_ms_ = 0;
    target_scroll_y_ = 0;

    current_scroll_y_ = height() / 2.0;

    layout_dirty_ = true;
    update();
}

void lyrics_widget::clear()
{
    lyrics_.clear();
    line_rects_.clear();
    current_index_ = -1;
    update();
}

void lyrics_widget::set_current_time(qint64 time_ms)
{
    current_time_ms_ = time_ms;

    if (lyrics_.isEmpty())
    {
        return;
    }

    int new_index = get_line_at_time(time_ms + kLyricPredictionOffsetMs);
    if (new_index != current_index_)
    {
        current_index_ = new_index;
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
    layout_dirty_ = true;
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
    if (lyrics_.isEmpty() || line_rects_.isEmpty())
    {
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
}

void lyrics_widget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

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

    int center_y = height() / 2;
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

        double dist_from_center = std::abs((screen_y + line_rect.height() / 2.0) - center_y);
        int alpha = 255;

        if (is_active)
        {
            painter.setFont(font_active_);
            painter.setPen(color_active_);
        }
        else
        {
            painter.setFont(font_normal_);

            double alpha_ratio = 1.0 - std::min(dist_from_center / (height() / 2.0), 1.0);
            alpha = static_cast<int>(100 + (155 * alpha_ratio * 0.5));

            QColor c = color_normal_;
            c.setAlpha(alpha);
            painter.setPen(c);
        }

        painter.drawText(line_rect, Qt::TextWordWrap | Qt::AlignCenter, lyrics_[i].text);
    }
}
