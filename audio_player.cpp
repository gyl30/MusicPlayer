#include <QDateTime>
#include <QMetaObject>
#include <QMediaDevices>
#include <numeric>

#include "log.h"
#include "scoped_exit.h"
#include "audio_player.h"

constexpr int kFeedIntervalMs = 50;
constexpr int kLongFeedIntervalMs = 1000;
constexpr int kProgressUpdateIntervalMs = 250;
constexpr auto kAudioBufferDurationSeconds = 2L;
constexpr auto kQueueBufferDurationSeconds = 5L;
constexpr auto kBufferLowWatermarkSeconds = 1L;

static int default_audio_bytes_second(const QAudioFormat& format) { return format.bytesPerFrame() * format.sampleRate(); }

audio_player::audio_player(QObject* parent) : QObject(parent)
{
    LOG_DEBUG("音频播放器已创建");

    feed_timer_ = new QTimer(this);
    feed_timer_->setSingleShot(true);
    feed_timer_->setTimerType(Qt::PreciseTimer);
    connect(feed_timer_, &QTimer::timeout, this, &audio_player::feed_audio_device);

    progress_timer_ = new QTimer(this);
    progress_timer_->setInterval(kProgressUpdateIntervalMs);
    connect(progress_timer_, &QTimer::timeout, this, &audio_player::update_progress_ui);
}

audio_player::~audio_player()
{
    stop_playback();
    LOG_DEBUG("音频播放器已销毁");
}

void audio_player::start_playback(qint64 session_id, const QAudioFormat& format, qint64 start_offset_ms)
{
    session_id_ = session_id;
    LOG_INFO("播放流程 8/14 播放器收到启动命令 会话ID {}", session_id_);
    LOG_DEBUG("音频参数: 采样率 {}, 声道数 {}, 样本格式 {}", format.sampleRate(), format.channelCount(), (int)format.sampleFormat());

    LOG_INFO("播放流程 9/14 重置播放器状态并启动音频设备 会话ID {}", session_id_);
    format_ = format;
    playback_start_offset_ms_ = start_offset_ms;
    decoder_finished_ = false;
    low_water_mark_triggered_ = true;

    buffer_low_water_mark_ = default_audio_bytes_second(format) * kBufferLowWatermarkSeconds;
    LOG_DEBUG("缓冲区低水位线设置为 {} 字节 ({} 秒)", buffer_low_water_mark_, kBufferLowWatermarkSeconds);

    data_queue_.clear();
    qint64 max_queue_size = default_audio_bytes_second(format) * kQueueBufferDurationSeconds;
    LOG_DEBUG("队列最大容量设置为 {} 字节 ({} 秒)", max_queue_size, kQueueBufferDurationSeconds);

    if (audio_sink_ != nullptr)
    {
        LOG_WARN("发现已存在的 audio_sink，将停止并删除它");
        audio_sink_->stop();
        delete audio_sink_;
    }

    audio_sink_ = new QAudioSink(QMediaDevices::defaultAudioOutput(), format_, this);
    auto buffer_size = default_audio_bytes_second(format_) * kAudioBufferDurationSeconds;
    audio_sink_->setBufferSize(buffer_size);
    connect(audio_sink_, &QAudioSink::stateChanged, this, &audio_player::on_sink_state_changed);
    LOG_DEBUG("音频设备已创建，缓冲区大小为 {} 字节 ({} 秒)", buffer_size, kAudioBufferDurationSeconds);

    io_device_ = audio_sink_->start();
    if (io_device_ == nullptr)
    {
        LOG_ERROR("启动音频设备IO失败，播放将无法开始");
        emit playback_error("启动音频设备失败");
        return;
    }
    LOG_INFO("音频设备启动成功");

    is_playing_ = true;

    QMetaObject::invokeMethod(this, "feed_audio_device", Qt::QueuedConnection);
    progress_timer_->start();
    LOG_INFO("会话 {} 所有定时器已在 {} 启动", session_id_, QDateTime::currentMSecsSinceEpoch());

    LOG_INFO("播放流程 10/14 播放器准备就绪通知控制中心 会话ID {}", session_id_);
    emit playback_ready(session_id_);
}

void audio_player::stop_playback()
{
    if (!is_playing_.load())
    {
        return;
    }
    LOG_INFO("停止流程 3/4 播放器收到停止命令会话ID {}", session_id_);
    is_playing_ = false;

    feed_timer_->stop();
    progress_timer_->stop();
    LOG_DEBUG("所有定时器已停止");

    data_queue_.clear();

    if (audio_sink_ != nullptr)
    {
        audio_sink_->stop();
        LOG_DEBUG("音频设备已停止");
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
        LOG_INFO("结束流程 2/4 收到来自控制中心的文件结束信号 会话ID {}", session_id_);
    }
    else
    {
        data_queue_.push_back(packet);
        low_water_mark_triggered_ = false;
    }

    if (queue_was_empty && !data_queue_.empty())
    {
        LOG_TRACE("队列曾为空，现有数据，通过 invokeMethod 触发数据供给");
        if (!feed_timer_->isActive())
        {
            QMetaObject::invokeMethod(this, "feed_audio_device", Qt::QueuedConnection);
        }
    }
}

void audio_player::handle_seek(qint64 session_id, qint64 actual_seek_ms)
{
    if (session_id != session_id_)
    {
        return;
    }
    LOG_INFO("跳转流程 7/10 播放器收到跳转处理请求 会话ID {} 实际毫秒 {}", session_id, actual_seek_ms);
    LOG_INFO("跳转流程 8/10 正在清空缓冲区并重置音频设备 会话ID {}", session_id);
    playback_start_offset_ms_ = actual_seek_ms;

    decoder_finished_ = false;
    low_water_mark_triggered_ = true;

    data_queue_.clear();
    LOG_DEBUG("播放器数据队列已为跳转清空");

    if (audio_sink_ != nullptr && audio_sink_->state() != QAudio::StoppedState)
    {
        audio_sink_->stop();
        LOG_DEBUG("音频设备已为跳转停止");
    }

    if (audio_sink_ != nullptr)
    {
        audio_sink_->reset();
        LOG_DEBUG("音频设备已重置");
    }
    LOG_INFO("跳转流程 8/10 跳转处理完毕，通知控制中心 会话ID {}", session_id);
    emit seek_handled(session_id_);
}

void audio_player::pause_feeding(qint64 session_id)
{
    if (session_id != session_id_)
    {
        return;
    }
    LOG_INFO("会话 {} 暂停数据供给", session_id_);
    feed_timer_->stop();
    progress_timer_->stop();
}

void audio_player::resume_feeding(qint64 session_id)
{
    if (session_id != session_id_)
    {
        return;
    }
    io_device_ = audio_sink_->start();
    if (io_device_ == nullptr)
    {
        LOG_ERROR("暂停或跳转后启动音频设备IO失败");
        is_playing_ = false;
    }
    else
    {
        LOG_DEBUG("跳转后音频设备成功重启");
        is_playing_ = true;
    }

    if (is_playing_)
    {
        LOG_INFO("会话 {} 恢复数据供给", session_id_);
        if (!feed_timer_->isActive())
        {
            QMetaObject::invokeMethod(this, "feed_audio_device", Qt::QueuedConnection);
        }
        progress_timer_->start();
    }
}

void audio_player::feed_audio_device()
{
    LOG_TRACE("会话 {} feed_audio_device 已触发", session_id_);

    if (!is_playing_ || io_device_ == nullptr || audio_sink_ == nullptr)
    {
        LOG_TRACE("会话 {} 数据供给中止，播放器未就绪", session_id_);
        return;
    }

    static bool is_feeding = false;
    if (is_feeding)
    {
        LOG_TRACE("会话 {} 因重入跳过此次数据供给", session_id_);
        return;
    }
    is_feeding = true;
    DEFER(is_feeding = false;);

    if (audio_sink_->state() == QAudio::StoppedState)
    {
        LOG_WARN("会话 {} 音频设备处于停止状态，不再安排数据供给", session_id_);
        return;
    }

    const qint64 total_buffer_bytes = audio_sink_->bufferSize();
    qint64 bytes_to_write = audio_sink_->bytesFree();
    qint64 bytes_buffered_before = total_buffer_bytes - bytes_to_write;

    LOG_TRACE("会话 {} 写入前缓冲区状态: 总计 {}, 已缓冲 {}, 空闲 {}", session_id_, total_buffer_bytes, bytes_buffered_before, bytes_to_write);

    if (data_queue_.empty())
    {
        LOG_TRACE("会话 {} 数据队列为空，无可写入内容", session_id_);
    }
    else if (bytes_to_write > 0)
    {
        qint64 bytes_written_this_cycle = 0;
        int packets_written_this_cycle = 0;

        while (!data_queue_.empty())
        {
            auto& next_packet = data_queue_.front();
            if (!next_packet || next_packet->data.empty())
            {
                LOG_WARN("会话 {} 在队列中发现空数据包，将其丢弃", session_id_);
                data_queue_.pop_front();
                continue;
            }

            auto packet_size = static_cast<qint64>(next_packet->data.size());
            if (bytes_to_write >= packet_size)
            {
                emit packet_played(next_packet);
                qint64 written_bytes = io_device_->write(reinterpret_cast<const char*>(next_packet->data.data()), packet_size);

                if (written_bytes != packet_size)
                {
                    LOG_WARN("会话 {} 向设备写入数据不足，预期 {} 字节，实际写入 {}", session_id_, packet_size, written_bytes);
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
                break;
            }
        }
        if (bytes_written_this_cycle > 0)
        {
            LOG_TRACE("会话 {} 写入 {} 个数据包，总计 {} 字节", session_id_, packets_written_this_cycle, bytes_written_this_cycle);
        }
    }

    if (!low_water_mark_triggered_)
    {
        qint64 remaining_bytes = std::accumulate(
            data_queue_.begin(), data_queue_.end(), 0LL, [](qint64 sum, const auto& packet) { return sum + (packet ? packet->data.size() : 0); });

        if (remaining_bytes < buffer_low_water_mark_)
        {
            LOG_TRACE("会话 {} 缓冲区水位低 ({} 字节)，请求更多数据", session_id_, remaining_bytes);
            emit buffer_level_low(session_id_);
            low_water_mark_triggered_ = true;
        }
    }

    if (data_queue_.empty() && decoder_finished_)
    {
        LOG_INFO("结束流程 3/4 数据队列已空且解码完成 等待音频设备空闲 会话ID {}", session_id_);
    }
    else if (is_playing_)
    {
        qint64 buffered_ms = 0;
        const auto bytes_per_second = format_.sampleRate() * format_.bytesPerFrame();

        if (bytes_per_second > 0)
        {
            const qint64 used_bytes = audio_sink_->bufferSize() - audio_sink_->bytesFree();
            buffered_ms = (used_bytes * 1000) / bytes_per_second;
        }

        qint64 next_interval_ms = (buffered_ms > kLongFeedIntervalMs) ? kLongFeedIntervalMs : kFeedIntervalMs;

        LOG_TRACE("下次数据供给计划在 {} 毫秒后，硬件缓冲区有 {} 毫秒的音频", next_interval_ms, buffered_ms);
        feed_timer_->start(static_cast<int>(next_interval_ms));
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

    LOG_DEBUG("会话 {} 进度更新: 已处理 {} 微秒，总计 {} 毫秒", session_id_, processed_us, playback_start_offset_ms_ + processed_ms);

    emit progress_update(session_id_, playback_start_offset_ms_ + processed_ms);
}

void audio_player::on_sink_state_changed(QAudio::State state)
{
    if (state == QAudio::IdleState)
    {
        if (is_playing_ && decoder_finished_ && data_queue_.empty())
        {
            LOG_INFO("结束流程 3/4 音频设备已空闲 通知控制中心播放已结束 会话ID {}", session_id_);
            is_playing_ = false;
            feed_timer_->stop();
            progress_timer_->stop();
            emit playback_finished(session_id_);
        }
    }
}
