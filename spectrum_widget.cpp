#include <QPainter>
#include <QPaintEvent>
#include <algorithm>
#include <cmath>
#include "fftreal.h"
#include "spectrum_widget.h"

constexpr int kNumBars = 25;
constexpr int kBlockHeight = 15;
constexpr int kBlockSpacing = 4;
constexpr double kMinDbRange = 20.0;
constexpr double kMaxDbDecayRate = 0.3;
constexpr double kMinDbRiseRate = 0.2;

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

void spectrum_widget::start_playback(qint64 start_offset_ms)
{
    render_timer_->stop();
    packet_queue_.clear();

    prev_magnitudes_.clear();
    target_magnitudes_.clear();
    display_magnitudes_.clear();
    prev_timestamp_ms_ = 0;
    target_timestamp_ms_ = 0;
    start_offset_ms_ = start_offset_ms;

    dynamic_min_db_ = 100.0;
    dynamic_max_db_ = 0.0;

    animation_clock_.start();
    render_timer_->start(80);
}

void spectrum_widget::stop_playback() { render_timer_->stop(); }

void spectrum_widget::on_render_timeout()
{
    qint64 current_playback_time = animation_clock_.elapsed() + start_offset_ms_;

    while (packet_queue_.size() >= 2 && current_playback_time >= packet_queue_[1]->ms)
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

        qint64 current_time = current_playback_time;
        qint64 interval_duration = target_timestamp_ms_ - prev_timestamp_ms_;
        qint64 time_in_interval = current_time - prev_timestamp_ms_;

        double t = (interval_duration > 0) ? (static_cast<double>(time_in_interval) / static_cast<double>(interval_duration)) : 0.0;
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
    painter.setPen(Qt::NoPen);

    if (display_magnitudes_.empty())
    {
        return;
    }

    std::vector<double> current_frame_db_values;
    current_frame_db_values.reserve(kNumBars);
    auto data_points_per_bar = display_magnitudes_.size() / kNumBars;
    if (data_points_per_bar == 0)
    {
        data_points_per_bar = 1;
    }
    for (int i = 0; i < kNumBars; ++i)
    {
        double avg_magnitude = 0;
        int count = 0;
        for (size_t j = i * data_points_per_bar; j < (i + 1) * data_points_per_bar && j < display_magnitudes_.size(); ++j)
        {
            avg_magnitude += display_magnitudes_[j];
            count++;
        }
        if (count > 0)
        {
            avg_magnitude /= count;
        }
        double db_value = 20 * log10(avg_magnitude + 1e-9);
        current_frame_db_values.push_back(qMax(0.0, db_value));
    }

    double current_frame_min_db = 1000.0;
    double current_frame_max_db = 0.0;
    for (double db : current_frame_db_values)
    {
        current_frame_min_db = std::min(db, current_frame_min_db);
        current_frame_max_db = std::max(db, current_frame_max_db);
    }
    if (current_frame_max_db > dynamic_max_db_)
    {
        dynamic_max_db_ = current_frame_max_db;
    }
    else
    {
        dynamic_max_db_ -= kMaxDbDecayRate;
        dynamic_max_db_ = std::max(dynamic_max_db_, current_frame_max_db);
    }
    if (current_frame_min_db < dynamic_min_db_)
    {
        dynamic_min_db_ = current_frame_min_db;
    }
    else
    {
        dynamic_min_db_ += kMinDbRiseRate;
        dynamic_min_db_ = std::min(dynamic_min_db_, current_frame_min_db);
    }

    double range = dynamic_max_db_ - dynamic_min_db_;
    range = std::max(range, kMinDbRange);

    QLinearGradient gradient(0, 0, 0, height());
    gradient.setColorAt(0.0, Qt::red);
    gradient.setColorAt(0.45, Qt::yellow);
    gradient.setColorAt(1.0, Qt::green);
    painter.setBrush(gradient);

    double total_bar_width = static_cast<double>(width()) / kNumBars;
    double bar_draw_width = total_bar_width * 0.8;

    for (int i = 0; i < kNumBars; ++i)
    {
        double db_value = current_frame_db_values[i];
        double height_ratio = (db_value - dynamic_min_db_) / range;
        double bar_height = qBound(0.0, height_ratio, 1.0) * height();

        if (bar_height <= 0)
        {
            continue;
        }

        double bar_x_position = (i * total_bar_width) + (total_bar_width * 0.1);
        double height_to_draw = bar_height;
        double current_block_bottom_y = height();
        while (height_to_draw > 0)
        {
            double current_block_h = std::min(height_to_draw, static_cast<double>(kBlockHeight));
            double block_top_y = current_block_bottom_y - current_block_h;
            QRectF block_rect(bar_x_position, block_top_y, bar_draw_width, current_block_h);
            painter.drawRect(block_rect);
            height_to_draw -= (kBlockHeight + kBlockSpacing);
            current_block_bottom_y -= (kBlockHeight + kBlockSpacing);
        }
    }
}
