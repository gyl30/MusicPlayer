#include <cmath>
#include "spectrum_processor.h"
#include "log.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double lerp(double a, double b, double t) { return a + (t * (b - a)); }

spectrum_processor::spectrum_processor(QObject* parent) : QObject(parent)
{
    render_timer_ = new QTimer(this);
    connect(render_timer_, &QTimer::timeout, this, &spectrum_processor::on_render_timeout);

    const int fft_size = 512;
    fft_transformer_ = std::make_unique<fft_real<double>>(fft_size);
    fft_input_buffer_.resize(fft_size);
}

std::vector<double> spectrum_processor::calculate_magnitudes(const std::shared_ptr<audio_packet>& packet)
{
    if (packet == nullptr)
    {
        return {};
    }

    const int fft_size = 512;

    const auto* pcm_data = reinterpret_cast<const qint16*>(packet->data.data());
    auto num_samples = packet->data.size() / sizeof(qint16);

    if (num_samples < fft_size)
    {
        return {};
    }

    for (size_t i = 0; i < fft_size; ++i)
    {
        double window = 0.5 * (1.0 - cos(2.0 * M_PI * static_cast<double>(i) / (fft_size - 1)));
        fft_input_buffer_[i] = (static_cast<double>(pcm_data[i]) / 32768.0) * window;
    }

    fft_transformer_->do_fft(fft_input_buffer_.data());

    std::vector<double> magnitudes;
    magnitudes.reserve(fft_size / 2);
    for (size_t i = 1; i < fft_size / 2; ++i)
    {
        double real = fft_transformer_->get_real(i);
        double imag = fft_transformer_->get_imag(i);
        magnitudes.push_back(std::sqrt((real * real) + (imag * imag)));
    }
    return magnitudes;
}

void spectrum_processor::process_packet(const std::shared_ptr<audio_packet>& packet)
{
    if (packet)
    {
        packet_queue_.push_back(packet);
    }
}

void spectrum_processor::reset_and_start(qint64 start_offset_ms)
{
    LOG_DEBUG("频谱处理器重置并启动, 偏移时间 {}ms", start_offset_ms);
    render_timer_->stop();
    packet_queue_.clear();

    prev_magnitudes_.clear();
    target_magnitudes_.clear();

    prev_timestamp_ms_ = 0;
    target_timestamp_ms_ = 0;
    start_offset_ms_ = start_offset_ms;

    needs_resync_ = start_offset_ms > 0;

    LOG_DEBUG("频谱处理器队列与状态已清除");

    animation_clock_.start();
    render_timer_->start(80);
}

void spectrum_processor::stop_playback()
{
    render_timer_->stop();
    packet_queue_.clear();
}

void spectrum_processor::on_render_timeout()
{
    if (!render_timer_->isActive())
    {
        return;
    }

    if (needs_resync_ && !packet_queue_.empty())
    {
        start_offset_ms_ = packet_queue_.front()->ms;
        animation_clock_.restart();
        needs_resync_ = false;
        LOG_DEBUG("频谱处理器时钟已与实际起始时间 {}ms 同步", start_offset_ms_);
    }

    qint64 current_playback_time = animation_clock_.elapsed() + start_offset_ms_;

    while (packet_queue_.size() >= 2 && current_playback_time >= packet_queue_[1]->ms)
    {
        packet_queue_.pop_front();
    }

    if (packet_queue_.empty())
    {
        return;
    }

    std::vector<double> display_magnitudes;

    if (packet_queue_.size() < 2)
    {
        const auto& current_packet = packet_queue_[0];
        if (current_packet->ms != target_timestamp_ms_)
        {
            target_magnitudes_ = calculate_magnitudes(current_packet);
            target_timestamp_ms_ = current_packet->ms;
        }
        display_magnitudes = target_magnitudes_;
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

        qint64 interval_duration = target_timestamp_ms_ - prev_timestamp_ms_;
        qint64 time_in_interval = current_playback_time - prev_timestamp_ms_;

        double t = (interval_duration > 0) ? (static_cast<double>(time_in_interval) / static_cast<double>(interval_duration)) : 0.0;
        t = qBound(0.0, t, 1.0);

        if (display_magnitudes.size() != target_magnitudes_.size())
        {
            display_magnitudes.resize(target_magnitudes_.size());
        }
        for (size_t i = 0; i < target_magnitudes_.size(); ++i)
        {
            display_magnitudes[i] = lerp(prev_magnitudes_[i], target_magnitudes_[i], t);
        }
    }

    if (!display_magnitudes.empty())
    {
        emit magnitudes_ready(display_magnitudes);
    }
}
