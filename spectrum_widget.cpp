#include <QPainter>
#include <QPaintEvent>
#include <QLinearGradient>
#include <algorithm>
#include <cmath>
#include "fftreal.h"
#include "spectrum_widget.h"

static double lerp(double a, double b, double t) { return a + (t * (b - a)); }

static std::vector<double> calculate_magnitudes(const std::shared_ptr<audio_packet>& packet)
{
    if (packet == nullptr)
    {
        return {};
    }

    const int fft_size = 1024;
    const auto* pcm_data = reinterpret_cast<const qint16*>(packet->data.data());
    auto num_samples = packet->data.size() / sizeof(qint16);

    if (num_samples < fft_size)
    {
        return {};
    }

    std::vector<double> fft_input(fft_size);
    for (int i = 0; i < fft_size; ++i)
    {
        fft_input[i] = static_cast<double>(pcm_data[i]) / 32768.0;
    }
    fft_real<double> fft(fft_size);
    fft.do_fft(fft_input.data());

    std::vector<double> magnitudes;
    magnitudes.reserve(fft_size / 2);
    for (size_t i = 1; i < fft_size / 2; ++i)
    {
        double real = fft.get_real(i);
        double imag = fft.get_imag(i);
        magnitudes.push_back(std::sqrt((real * real) + (imag * imag)));
    }
    return magnitudes;
}

spectrum_widget::spectrum_widget(QWidget* parent) : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    setMinimumHeight(100);
    render_timer_ = new QTimer(this);
    connect(render_timer_, &QTimer::timeout, this, &spectrum_widget::on_render_timeout);
}

void spectrum_widget::enqueue_packet(const std::shared_ptr<audio_packet>& packet) { packet_queue_.push_back(packet); }

void spectrum_widget::start_playback()
{
    render_timer_->stop();
    packet_queue_.clear();
    prev_magnitudes_.clear();
    target_magnitudes_.clear();
    display_magnitudes_.clear();
    min_rendered_db_ = 1000.0;
    max_rendered_db_ = 0.0;
    prev_timestamp_ms_ = 0;
    target_timestamp_ms_ = 0;
    update();
    animation_clock_.start();
    render_timer_->start(150);
}

void spectrum_widget::stop_playback() { render_timer_->stop(); }

void spectrum_widget::on_render_timeout()
{
    while (packet_queue_.size() >= 2 && animation_clock_.elapsed() >= packet_queue_[1]->ms)
    {
        packet_queue_.erase(packet_queue_.begin());
    }

    if (packet_queue_.empty())
    {
        return;
    }

    if (packet_queue_.size() < 2)
    {
        const auto& current_packet = packet_queue_[0];
        if (current_packet->ms != target_timestamp_ms_)
        {
            target_magnitudes_ = calculate_magnitudes(current_packet);
            target_timestamp_ms_ = current_packet->ms;
            display_magnitudes_ = target_magnitudes_;
        }
    }
    else
    {
        const auto& prev_packet = packet_queue_[0];
        const auto& target_packet = packet_queue_[1];
        if (prev_packet->ms != prev_timestamp_ms_)
        {
            prev_magnitudes_ = calculate_magnitudes(prev_packet);
            prev_timestamp_ms_ = prev_packet->ms;
        }
        if (target_packet->ms != target_timestamp_ms_)
        {
            target_magnitudes_ = calculate_magnitudes(target_packet);
            target_timestamp_ms_ = target_packet->ms;
        }

        if (prev_magnitudes_.empty() || target_magnitudes_.empty() || prev_magnitudes_.size() != target_magnitudes_.size())
        {
            return;
        }

        qint64 current_time = animation_clock_.elapsed();
        qint64 interval_duration = target_timestamp_ms_ - prev_timestamp_ms_;
        qint64 time_in_interval = current_time - prev_timestamp_ms_;

        double t = 0.0;
        if (interval_duration > 0)
        {
            t = static_cast<double>(time_in_interval) / static_cast<double>(interval_duration);
        }
        t = qBound(0.0, t, 1.0);

        if (display_magnitudes_.size() != target_magnitudes_.size())
        {
            display_magnitudes_.resize(target_magnitudes_.size());
        }
        for (size_t i = 0; i < target_magnitudes_.size(); ++i)
        {
            display_magnitudes_[i] = lerp(prev_magnitudes_[i], target_magnitudes_[i], t);
        }
    }

    update();
}

void spectrum_widget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    QPainter painter(this);

    if (display_magnitudes_.empty())
    {
        return;
    }

    const int num_bars_to_display = 28;
    std::vector<double> current_frame_db_values;
    current_frame_db_values.reserve(num_bars_to_display);

    auto data_points_per_bar = display_magnitudes_.size() / num_bars_to_display;
    if (data_points_per_bar == 0)
    {
        data_points_per_bar = 1;
    }

    for (int i = 0; i < num_bars_to_display; ++i)
    {
        auto start_index = i * data_points_per_bar;
        auto end_index = start_index + data_points_per_bar;

        double average_magnitude = 0;
        if (start_index < display_magnitudes_.size())
        {
            int count = 0;
            for (auto j = start_index; j < end_index && j < display_magnitudes_.size(); ++j)
            {
                average_magnitude += display_magnitudes_[j];
                count++;
            }
            if (count > 0)
            {
                average_magnitude /= count;
            }
        }

        double db_value = 20 * log10(average_magnitude + 1e-9);
        db_value = qMax(0.0, db_value);
        current_frame_db_values.push_back(db_value);

        min_rendered_db_ = std::min(db_value, min_rendered_db_);
        max_rendered_db_ = std::max(db_value, max_rendered_db_);
    }

    double range = max_rendered_db_ - min_rendered_db_;
    range = std::max(range, 15.0);

    const int bar_spacing = 2;
    double total_bar_width = static_cast<double>(width()) / num_bars_to_display;
    double bar_draw_width = total_bar_width - bar_spacing;
    bar_draw_width = std::max<double>(bar_draw_width, 1);

    for (int i = 0; i < num_bars_to_display; ++i)
    {
        double db_value = current_frame_db_values[i];
        double bar_height_ratio = (db_value - min_rendered_db_) / range;
        bar_height_ratio = std::max(0.0, std::min(bar_height_ratio, 1.0));
        double bar_height = bar_height_ratio * height();
        double bar_x_position = i * total_bar_width;

        QRectF bar(bar_x_position, height() - bar_height, bar_draw_width, bar_height);

        QLinearGradient gradient(bar.topLeft(), bar.bottomLeft());
        gradient.setColorAt(1.0, Qt::green);
        gradient.setColorAt(0.5, Qt::yellow);
        gradient.setColorAt(0.0, Qt::red);

        painter.fillRect(bar, gradient);
    }
}
