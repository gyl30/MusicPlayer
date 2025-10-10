#include <QMediaDevices>

#include "log.h"
#include "audio_player.h"

constexpr auto kAudioBufferDurationSeconds = 2L;
constexpr auto kQueueBufferDurationSeconds = 5L;

static int default_audio_bytes_second(const QAudioFormat& format) { return format.bytesPerFrame() * format.sampleRate(); }

audio_player::audio_player(QObject* parent) : QObject(parent)
{
    LOG_DEBUG("AudioPlayer created.");

    feed_timer_ = new QTimer(this);
    feed_timer_->setSingleShot(false);
    feed_timer_->setTimerType(Qt::PreciseTimer);
    connect(feed_timer_, &QTimer::timeout, this, &audio_player::feed_audio_device);

    progress_timer_ = new QTimer(this);
    progress_timer_->setInterval(250);
    connect(progress_timer_, &QTimer::timeout, this, &audio_player::update_progress_ui);
}

audio_player::~audio_player()
{
    stop_playback();
    LOG_DEBUG("audio player destroyed");
}

void audio_player::start_playback(const QAudioFormat& format, qint64 start_offset_ms)
{
    LOG_INFO("received start playback command offset {}ms", start_offset_ms);
    LOG_DEBUG("sample rate {} channels {} sample format {}", format.sampleRate(), format.channelCount(), (int)format.sampleFormat());

    format_ = format;
    playback_start_offset_ms_ = start_offset_ms;
    decoder_finished_ = false;

    data_queue_.clear();
    uint64_t max_queue_size = default_audio_bytes_second(format_) * kQueueBufferDurationSeconds;
    LOG_DEBUG("queue max size set to {} bytes {} seconds", max_queue_size, kQueueBufferDurationSeconds);

    if (audio_sink_ != nullptr)
    {
        LOG_WARN("an existing audio_sink was found stopping and deleting it");
        audio_sink_->stop();
        delete audio_sink_;
    }

    audio_sink_ = new QAudioSink(QMediaDevices::defaultAudioOutput(), format_, this);
    auto buffer_size = default_audio_bytes_second(format_) * kAudioBufferDurationSeconds;
    audio_sink_->setBufferSize(buffer_size);
    LOG_DEBUG("audio sink created with a buffer size of {} bytes {} seconds", buffer_size, kAudioBufferDurationSeconds);

    io_device_ = audio_sink_->start();
    if (io_device_ == nullptr)
    {
        LOG_ERROR("failed to start sink io device playback will not start");
        return;
    }
    LOG_INFO("audio sink started successfully");

    is_playing_ = true;

    feed_audio_device();

    feed_timer_->start(50);
    progress_timer_->start();
    LOG_DEBUG("all timers started");
}

void audio_player::stop_playback()
{
    if (!is_playing_.load())
    {
        return;
    }
    LOG_INFO("received stop playback command");
    is_playing_ = false;
    data_queue_.clear();
    feed_timer_->stop();
    progress_timer_->stop();
    LOG_DEBUG("all timers stopped");

    if (audio_sink_ != nullptr)
    {
        audio_sink_->stop();
        LOG_DEBUG("audio sink stopped");
    }
    io_device_ = nullptr;
}

void audio_player::enqueue_packet(const std::shared_ptr<audio_packet>& packet)
{
    if (packet == nullptr)
    {
        decoder_finished_ = true;
        LOG_DEBUG("end of stream signal nullptr packet received");
    }
    else
    {
        data_queue_.enqueue(packet);
        LOG_TRACE("timestamp {} size {} bytes queue size is now {} bytes", packet->ms, packet->data.size(), data_queue_.size_in_bytes());
    }
}

void audio_player::handle_seek(qint64 actual_seek_ms)
{
    LOG_INFO("seek to {}ms", actual_seek_ms);
    playback_start_offset_ms_ = actual_seek_ms;
    decoder_finished_ = false;

    data_queue_.clear();

    if (audio_sink_ != nullptr && audio_sink_->state() != QAudio::StoppedState)
    {
        audio_sink_->stop();
        LOG_DEBUG("audio sink stopped for seek");
    }

    LOG_DEBUG("clearing data queue for seek");

    if (audio_sink_ != nullptr)
    {
        audio_sink_->reset();
        LOG_DEBUG("audio sink reset");
        io_device_ = audio_sink_->start();
        if (io_device_ == nullptr)
        {
            LOG_ERROR("failed to restart sink io device after seek");
            is_playing_ = false;
        }
        else
        {
            LOG_DEBUG("audio sink restarted successfully after seek");
        }
    }
}

void audio_player::pause_feeding()
{
    LOG_DEBUG("pausing data feeding all timers stopped");
    feed_timer_->stop();
    progress_timer_->stop();
}

void audio_player::resume_feeding()
{
    if (is_playing_)
    {
        LOG_DEBUG("resuming data feeding all timers started");
        feed_timer_->start(50);
        progress_timer_->start();
    }
}

void audio_player::feed_audio_device()
{
    LOG_TRACE("feed audio device triggered");

    if (!is_playing_ || io_device_ == nullptr || audio_sink_ == nullptr)
    {
        LOG_TRACE("aborted not playing or device null");
        return;
    }

    if (audio_sink_->state() == QAudio::StoppedState)
    {
        LOG_WARN("audio sink is in stopped stopping timer");
        feed_timer_->stop();
        progress_timer_->stop();
        return;
    }
    if (audio_sink_->state() == QAudio::IdleState)
    {
        LOG_WARN("audio sink is in idle might indicate a buffer underrun");
    }

    const int bytes_per_sec = default_audio_bytes_second(format_);
    const qint64 total_buffer_bytes = audio_sink_->bufferSize();
    const qint64 free_bytes = audio_sink_->bytesFree();
    const qint64 bytes_buffered = total_buffer_bytes - free_bytes;
    const auto low_water_mark_bytes = bytes_per_sec * 1;

    LOG_TRACE("buffer status total {} free {} buffer {} {} low watermark {}",
              total_buffer_bytes,
              free_bytes,
              bytes_buffered,
              (bytes_buffered * 1000) / (bytes_per_sec > 0 ? bytes_per_sec : 1),
              low_water_mark_bytes);

    if (bytes_buffered < low_water_mark_bytes)
    {
        LOG_TRACE("buffered data is below low watermark attempting to write more data");
        auto bytes_written_this_cycle = 0UL;
        while (audio_sink_->bytesFree() > 0 && !data_queue_.is_empty())
        {
            auto packet = data_queue_.try_dequeue();
            if (packet)
            {
                io_device_->write(reinterpret_cast<const char*>(packet->data.data()), static_cast<qint64>(packet->data.size()));
                bytes_written_this_cycle += packet->data.size();
                emit spectrum_data_ready(packet);
            }
        }
        if (bytes_written_this_cycle > 0)
        {
            LOG_TRACE("write {} bytes to audio device", bytes_written_this_cycle);
        }
        else
        {
            LOG_TRACE("no data written queue is empty");
        }
    }

    const qint64 current_buffered_ms = (bytes_buffered * 1000) / (bytes_per_sec > 0 ? bytes_per_sec : 1);
    const int new_interval = current_buffered_ms < 1000 ? 50 : 200;
    if (feed_timer_->interval() != new_interval)
    {
        feed_timer_->setInterval(new_interval);
        LOG_TRACE("timer interval adjusted to {}ms", new_interval);
    }

    if (data_queue_.is_empty() && decoder_finished_)
    {
        LOG_DEBUG("queue is empty and decoder has finished");
        const qint64 final_bytes_buffered = audio_sink_->bufferSize() - audio_sink_->bytesFree();
        if (final_bytes_buffered > 100)
        {
            const qint64 remaining_ms = (final_bytes_buffered * 1000) / bytes_per_sec;
            LOG_DEBUG("{} bytes remaining in buffer {}ms scheduling final stop", final_bytes_buffered, remaining_ms);
            QTimer::singleShot(remaining_ms + 100, this, &audio_player::playback_finished);
        }
        else
        {
            LOG_DEBUG("buffer is empty");
            emit playback_finished();
        }
        feed_timer_->stop();
        progress_timer_->stop();
    }
}

void audio_player::update_progress_ui()
{
    if (!is_playing_ || audio_sink_ == nullptr)
    {
        return;
    }

    const qint64 processed_ms = audio_sink_->processedUSecs() / 1000;
    emit progress_update(playback_start_offset_ms_ + processed_ms);
}
