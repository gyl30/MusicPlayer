#include <QMediaDevices>
#include <QDateTime>
#include <QMetaObject>

#include "log.h"
#include "audio_player.h"
#include "scoped_exit.h"

constexpr auto kAudioBufferDurationSeconds = 2L;
constexpr auto kQueueBufferDurationSeconds = 5L;

static int default_audio_bytes_second(const QAudioFormat& format) { return format.bytesPerFrame() * format.sampleRate(); }

audio_player::audio_player(QObject* parent) : QObject(parent)
{
    LOG_DEBUG("audio player created");

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

void audio_player::start_playback(qint64 session_id, const QAudioFormat& format, qint64 start_offset_ms)
{
    session_id_ = session_id;
    LOG_INFO("session {} received start playback command offset {}ms", session_id_, start_offset_ms);
    LOG_DEBUG("sample rate {} channels {} sample format {}", format.sampleRate(), format.channelCount(), (int)format.sampleFormat());

    format_ = format;
    playback_start_offset_ms_ = start_offset_ms;
    decoder_finished_ = false;

    data_queue_.clear();
    qint64 max_queue_size = default_audio_bytes_second(format) * kQueueBufferDurationSeconds;
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
        emit playback_error("Failed to start audio sink device.");
        return;
    }
    LOG_INFO("audio sink started successfully");

    is_playing_ = true;

    feed_timer_->start(50);
    progress_timer_->start();
    LOG_INFO("session {} all timers started at {}", session_id_, QDateTime::currentMSecsSinceEpoch());

    emit playback_ready(session_id_);
}

void audio_player::stop_playback()
{
    if (!is_playing_.load())
    {
        return;
    }
    LOG_INFO("session {} received stop playback command", session_id_);
    is_playing_ = false;

    feed_timer_->stop();
    progress_timer_->stop();
    LOG_DEBUG("all timers stopped");

    data_queue_.clear();

    if (audio_sink_ != nullptr)
    {
        audio_sink_->stop();
        LOG_DEBUG("audio sink stopped");
    }
    io_device_ = nullptr;
}

void audio_player::enqueue_packet(qint64 session_id, const std::shared_ptr<audio_packet>& packet)
{
    if (session_id != session_id_)
    {
        return;
    }

    bool queue_was_empty = data_queue_.empty();

    if (packet == nullptr)
    {
        decoder_finished_ = true;
        LOG_DEBUG("end of stream signal nullptr packet received");
    }
    else
    {
        data_queue_.push_back(packet);
    }

    if (queue_was_empty && !data_queue_.empty())
    {
        LOG_TRACE("queue was empty now has data triggering feed via invokeMethod");
        QMetaObject::invokeMethod(this, "feed_audio_device", Qt::QueuedConnection);
    }
}

void audio_player::handle_seek(qint64 session_id, qint64 actual_seek_ms)
{
    if (session_id != session_id_)
    {
        return;
    }
    LOG_INFO("session {} handling seek to {}ms", session_id_, actual_seek_ms);
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

void audio_player::pause_feeding(qint64 session_id)
{
    if (session_id != session_id_)
    {
        return;
    }
    LOG_INFO("session {} pausing data feeding", session_id_);
    feed_timer_->stop();
    progress_timer_->stop();
}

void audio_player::resume_feeding(qint64 session_id)
{
    if (session_id != session_id_)
    {
        return;
    }
    if (is_playing_)
    {
        LOG_INFO("session {} resuming data feeding", session_id_);
        feed_timer_->start(50);
        progress_timer_->start();
    }
}

void audio_player::feed_audio_device()
{
    LOG_TRACE("session {} feed_audio_device triggered", session_id_);

    if (!is_playing_ || io_device_ == nullptr || audio_sink_ == nullptr)
    {
        LOG_TRACE("session {} feed aborted player not ready", session_id_);
        return;
    }

    static bool is_feeding = false;
    if (is_feeding)
    {
        LOG_TRACE("session {} feed skipped due to re-entrancy", session_id_);
        return;
    }
    is_feeding = true;
    DEFER(is_feeding = false;);

    if (audio_sink_->state() == QAudio::StoppedState)
    {
        LOG_WARN("session {} audio sink is in stopped state stopping timer", session_id_);
        feed_timer_->stop();
        progress_timer_->stop();
        return;
    }

    const qint64 total_buffer_bytes = audio_sink_->bufferSize();
    qint64 bytes_to_write = audio_sink_->bytesFree();
    qint64 bytes_buffered_before = total_buffer_bytes - bytes_to_write;

    LOG_TRACE(
        "session {} buffer status before write total {} buffered {} free {}", session_id_, total_buffer_bytes, bytes_buffered_before, bytes_to_write);

    if (data_queue_.empty())
    {
        LOG_TRACE("session {} data queue is empty nothing to write", session_id_);
    }
    else if (bytes_to_write <= 0)
    {
        LOG_TRACE("session {} buffer is full cannot write", session_id_);
    }
    else
    {
        qint64 bytes_written_this_cycle = 0;
        int packets_written_this_cycle = 0;

        while (!data_queue_.empty())
        {
            auto& next_packet = data_queue_.front();
            if (!next_packet || next_packet->data.empty())
            {
                LOG_WARN("session {} found empty packet in queue discarding", session_id_);
                data_queue_.pop_front();
                continue;
            }

            auto packet_size = static_cast<qint64>(next_packet->data.size());
            LOG_TRACE("session {} trying to write packet size {} into free space {}", session_id_, packet_size, bytes_to_write);

            if (bytes_to_write >= packet_size)
            {
                emit packet_played(next_packet);
                qint64 written_bytes = io_device_->write(reinterpret_cast<const char*>(next_packet->data.data()), packet_size);

                if (written_bytes != packet_size)
                {
                    LOG_WARN("session {} short write to device expected {} but wrote {}", session_id_, packet_size, written_bytes);
                    if (written_bytes <= 0)
                    {
                        break;
                    }
                }

                bytes_written_this_cycle += written_bytes;
                packets_written_this_cycle++;
                bytes_to_write -= written_bytes;
                data_queue_.pop_front();
            }
            else
            {
                LOG_TRACE("session {} buffer space not enough for next packet breaking loop", session_id_);
                break;
            }
        }

        if (bytes_written_this_cycle > 0)
        {
            LOG_INFO(
                "session {} write summary wrote {} packets totaling {} bytes", session_id_, packets_written_this_cycle, bytes_written_this_cycle);
        }
    }

    if (data_queue_.empty() && decoder_finished_)
    {
        LOG_INFO("session {} queue is empty and decoder has finished checking for playback end", session_id_);
        const qint64 final_bytes_buffered = audio_sink_->bufferSize() - audio_sink_->bytesFree();
        if (final_bytes_buffered > 100)
        {
            const qint64 remaining_ms = (final_bytes_buffered * 1000) / default_audio_bytes_second(format_);
            LOG_INFO("session {} {} bytes remaining in buffer ({}ms) scheduling final stop", session_id_, final_bytes_buffered, remaining_ms);
            QTimer::singleShot(remaining_ms + 100, this, [this]() { emit playback_finished(session_id_); });
        }
        else
        {
            LOG_INFO("session {} buffer is empty playback finished", session_id_);
            emit playback_finished(session_id_);
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

    const qint64 processed_us = audio_sink_->processedUSecs();
    const qint64 processed_ms = processed_us / 1000;

    LOG_DEBUG("session {} progress update processed_us {} total_ms {}", session_id_, processed_us, playback_start_offset_ms_ + processed_ms);

    emit progress_update(session_id_, playback_start_offset_ms_ + processed_ms);
}
