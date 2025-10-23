#include <QMutexLocker>
#include <QMetaObject>
#include <numeric>
#include <algorithm>
#include "log.h"
#include "audio_player.h"

constexpr int kProgressUpdateIntervalMs = 250;
constexpr auto kBufferLowWatermarkSeconds = 2L;

void audio_player::audio_callback(void* userdata, Uint8* stream, int len)
{
    auto* player = static_cast<audio_player*>(userdata);
    if (player != nullptr)
    {
        player->fill_audio_buffer(stream, len);
    }
}

audio_player::audio_player(QObject* parent) : QObject(parent)
{
    if (SDL_Init(SDL_INIT_AUDIO) < 0)
    {
        LOG_ERROR("无法初始化 sdl 音频 {}", SDL_GetError());
    }
    else
    {
        LOG_DEBUG("sdl 音频子系统已初始化");
    }

    progress_timer_ = new QTimer(this);
    progress_timer_->setInterval(kProgressUpdateIntervalMs);
    connect(progress_timer_, &QTimer::timeout, this, &audio_player::update_progress_ui);
}

audio_player::~audio_player()
{
    stop_playback();
    SDL_Quit();
    LOG_DEBUG("音频播放器已销毁 sdl 已退出");
}

void audio_player::set_volume(int volume_percent)
{
    volume_ = static_cast<float>(qBound(0, volume_percent, 100)) / 100.0F;
    LOG_DEBUG("音量已设置为 {}", volume_.load());
}

void audio_player::on_playback_completed_internal()
{
    if (!is_playing_.load() || session_id_ == 0)
    {
        return;
    }

    LOG_INFO("结束流程 3/4 sdl 缓冲区播放完毕 暂停设备并通知控制中心 会话id {}", session_id_);

    is_playing_ = false;
    progress_timer_->stop();

    if (device_id_ != 0)
    {
        SDL_PauseAudioDevice(device_id_, 1);
    }

    emit playback_finished(session_id_);
}

void audio_player::start_playback(qint64 session_id, const QAudioFormat& format, qint64 start_offset_ms)
{
    session_id_ = session_id;
    LOG_INFO("播放流程 8/14 sdl 播放器收到启动命令 会话id {}", session_id_);

    stop_playback();

    last_format_ = format;

    SDL_AudioSpec desired_spec;
    SDL_zero(desired_spec);
    desired_spec.freq = format.sampleRate();
    desired_spec.channels = static_cast<uint8_t>(format.channelCount());
    desired_spec.format = AUDIO_S16LSB;
    desired_spec.silence = 0;
    desired_spec.samples = 2048;
    desired_spec.callback = audio_callback;
    desired_spec.userdata = this;

    device_id_ = SDL_OpenAudioDevice(nullptr, 0, &desired_spec, &audio_spec_, 0);

    if (device_id_ == 0)
    {
        LOG_ERROR("sdl 打开音频设备失败 {}", SDL_GetError());
        emit playback_error(QString("打开音频设备失败 %1").arg(SDL_GetError()));
        return;
    }
    LOG_INFO("sdl 音频设备已打开 id {}", device_id_);
    LOG_DEBUG("sdl 设备参数 频率 {}hz 声道 {} 格式 {} 缓冲区大小 {} 字节",
              audio_spec_.freq,
              audio_spec_.channels,
              (int)audio_spec_.format,
              audio_spec_.size);

    playback_start_offset_ms_ = start_offset_ms;
    bytes_processed_by_device_ = 0;
    decoder_finished_ = false;
    is_playing_ = true;

    qint64 bytes_per_second = static_cast<qint64>(audio_spec_.freq) * audio_spec_.channels * static_cast<qint64>(sizeof(qint16));
    buffer_low_water_mark_ = bytes_per_second * kBufferLowWatermarkSeconds;
    low_water_mark_triggered_ = true;
    LOG_DEBUG("sdl 缓冲区低水位线设置为 {} 字节 {} 秒", buffer_low_water_mark_, kBufferLowWatermarkSeconds);

    progress_timer_->start();
    SDL_PauseAudioDevice(device_id_, 0);

    LOG_INFO("播放流程 10/14 sdl 播放器准备就绪 通知控制中心 会话id {}", session_id_);
    emit playback_ready(session_id_);
}

void audio_player::stop_playback()
{
    if (!is_playing_.load() && device_id_ == 0)
    {
        return;
    }
    LOG_INFO("停止流程 3/4 sdl 播放器收到停止命令 会话id {}", session_id_);
    is_playing_ = false;

    progress_timer_->stop();

    if (device_id_ != 0)
    {
        SDL_LockAudioDevice(device_id_);
        SDL_PauseAudioDevice(device_id_, 1);
        SDL_CloseAudioDevice(device_id_);
        device_id_ = 0;
        SDL_UnlockAudioDevice(device_id_);
        LOG_DEBUG("sdl 音频设备已关闭");
    }

    QMutexLocker locker(&queue_mutex_);
    data_queue_.clear();
}

void audio_player::enqueue_packet(qint64 session_id, const std::shared_ptr<audio_packet>& packet)
{
    if (session_id != session_id_)
    {
        return;
    }

    if (packet == nullptr)
    {
        LOG_INFO("结束流程 2/4 sdl 收到来自控制中心的文件结束信号");
        decoder_finished_ = true;
    }
    else
    {
        QMutexLocker locker(&queue_mutex_);
        data_queue_.push_back(packet);
        low_water_mark_triggered_ = false;
    }
}

void audio_player::handle_seek(qint64 session_id, qint64 actual_seek_ms)
{
    if (session_id != session_id_)
    {
        return;
    }
    LOG_INFO("跳转流程 7/10 sdl 播放器收到跳转处理请求");

    if (device_id_ != 0)
    {
        SDL_CloseAudioDevice(device_id_);
        device_id_ = 0;
        LOG_DEBUG("sdl 音频设备已为跳转关闭");
    }

    if (!last_format_.isValid())
    {
        LOG_ERROR("sdl 无法重新打开设备 最后一次使用的音频格式无效");
        emit playback_error("无法在播放结束后跳转 音频格式信息已丢失");
        return;
    }

    SDL_AudioSpec desired_spec;
    SDL_zero(desired_spec);
    desired_spec.freq = last_format_.sampleRate();
    desired_spec.channels = static_cast<uint8_t>(last_format_.channelCount());
    desired_spec.format = AUDIO_S16LSB;
    desired_spec.silence = 0;
    desired_spec.samples = 2048;
    desired_spec.callback = audio_callback;
    desired_spec.userdata = this;

    device_id_ = SDL_OpenAudioDevice(nullptr, 0, &desired_spec, &audio_spec_, 0);
    if (device_id_ == 0)
    {
        LOG_ERROR("sdl 在 seek 期间重新打开音频设备失败 {}", SDL_GetError());
        emit playback_error("为跳转操作重新打开音频设备失败");
        return;
    }
    LOG_INFO("sdl 音频设备已为 seek 重新打开 id {}", device_id_);

    {
        QMutexLocker locker(&queue_mutex_);
        data_queue_.clear();
        LOG_INFO("跳转流程 8/10 sdl 播放器数据队列已为跳转清空");
    }

    playback_start_offset_ms_ = actual_seek_ms;
    bytes_processed_by_device_ = 0;
    decoder_finished_ = false;
    low_water_mark_triggered_ = true;
    is_playing_ = true;

    if (!progress_timer_->isActive())
    {
        progress_timer_->start();
    }

    LOG_INFO("跳转流程 8/10 sdl 跳转处理完毕 通知控制中心");
    emit seek_handled(session_id_);
}

void audio_player::pause_feeding(qint64 session_id) const
{
    if (session_id != session_id_ || device_id_ == 0)
    {
        return;
    }
    SDL_PauseAudioDevice(device_id_, 1);
    LOG_INFO("sdl 音频设备已暂停");
}

void audio_player::resume_feeding(qint64 session_id) const
{
    if (session_id != session_id_ || device_id_ == 0)
    {
        return;
    }
    SDL_PauseAudioDevice(device_id_, 0);
    LOG_INFO("sdl 音频设备已恢复");
}

void audio_player::fill_audio_buffer(Uint8* stream, int len)
{
    SDL_memset(stream, 0, static_cast<size_t>(len));

    bool should_finish = false;
    {
        QMutexLocker locker(&queue_mutex_);
        if (data_queue_.empty() && decoder_finished_)
        {
            should_finish = true;
        }
    }

    if (should_finish)
    {
        decoder_finished_ = false;
        QMetaObject::invokeMethod(this, "on_playback_completed_internal", Qt::QueuedConnection);
        return;
    }

    int bytes_filled_this_cycle = 0;
    {
        QMutexLocker locker(&queue_mutex_);
        if (!is_playing_ || data_queue_.empty())
        {
            return;
        }

        int bytes_to_fill = len;
        auto* stream_ptr = reinterpret_cast<int16_t*>(stream);

        while (bytes_to_fill > 0 && !data_queue_.empty())
        {
            auto& packet = data_queue_.front();

            const auto* packet_data = reinterpret_cast<const int16_t*>(packet->data.data());
            const size_t packet_samples = packet->data.size() / sizeof(int16_t);
            const size_t samples_to_fill = static_cast<size_t>(bytes_to_fill) / sizeof(int16_t);
            const size_t samples_to_copy = std::min(packet_samples, samples_to_fill);

            float current_volume = volume_.load();
            for (size_t i = 0; i < samples_to_copy; ++i)
            {
                stream_ptr[i] = static_cast<int16_t>(static_cast<float>(packet_data[i]) * current_volume);
            }

            emit packet_played(packet);
            stream_ptr += samples_to_copy;

            size_t bytes_copied = samples_to_copy * sizeof(int16_t);
            bytes_to_fill -= static_cast<int>(bytes_copied);

            if (bytes_copied == packet->data.size())
            {
                data_queue_.pop_front();
            }
            else
            {
                packet->data.erase(packet->data.begin(), packet->data.begin() + static_cast<int>(bytes_copied));
            }
        }
        bytes_filled_this_cycle = len - bytes_to_fill;
    }
    bytes_processed_by_device_ += bytes_filled_this_cycle;

    if (!low_water_mark_triggered_)
    {
        qint64 remaining_bytes;
        {
            QMutexLocker locker(&queue_mutex_);
            remaining_bytes = std::accumulate(data_queue_.begin(),
                                              data_queue_.end(),
                                              0LL,
                                              [](qint64 sum, const auto& pkt) { return sum + (pkt ? static_cast<qint64>(pkt->data.size()) : 0); });
        }

        if (remaining_bytes < buffer_low_water_mark_)
        {
            LOG_TRACE("sdl 缓冲区水位低 {} 字节 请求更多数据", remaining_bytes);
            QMetaObject::invokeMethod(this, "buffer_level_low", Qt::QueuedConnection, Q_ARG(qint64, session_id_));
            low_water_mark_triggered_ = true;
        }
    }
}

void audio_player::update_progress_ui()
{
    if (!is_playing_ || device_id_ == 0)
    {
        return;
    }

    qint64 bytes_per_second = static_cast<qint64>(audio_spec_.freq) * audio_spec_.channels * static_cast<qint64>(sizeof(qint16));
    if (bytes_per_second == 0)
    {
        return;
    }

    qint64 processed_bytes = bytes_processed_by_device_.load();
    qint64 processed_ms = (processed_bytes * 1000) / bytes_per_second;
    qint64 current_playback_ms = playback_start_offset_ms_ + processed_ms;

    LOG_DEBUG("播放器时钟 start_offset_ms {} processed_bytes {} processed_ms {} final_ms {}",
              playback_start_offset_ms_,
              processed_bytes,
              processed_ms,
              current_playback_ms);

    emit progress_update(session_id_, current_playback_ms);
}
