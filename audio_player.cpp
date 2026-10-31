#include <cmath>
#include <algorithm>
#include <SDL.h>
#include "log.h"
#include "audio_player.h"

constexpr int kProgressUpdateIntervalMs = 50;
constexpr int kPacketEnqueueRetryDelayMs = 2;
constexpr int kPacketEnqueueMaxWaitMs = 2000;
constexpr auto kBufferLowWatermarkSeconds = 2L;
constexpr auto kBufferHighWatermarkSeconds = 5L;

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
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
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
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    LOG_DEBUG("音频播放器已销毁 sdl 音频子系统已退出");
}

void audio_player::clear_queue()
{
    packet_queue_.consume_all([](const std::shared_ptr<audio_packet>&) {});
    current_packet_.reset();
    played_packets_.consume_all([](const std::shared_ptr<audio_packet>&) {});
    approx_buffered_bytes_.store(0);
}

void audio_player::set_volume(int volume_percent)
{
    int clamped_percent = qBound(0, volume_percent, 100);
    float factor = std::pow(static_cast<float>(clamped_percent) / 100.0F, 3.0F);
    int sdl_volume = static_cast<int>(factor * SDL_MIX_MAXVOLUME);

    volume_.store(sdl_volume);
    LOG_DEBUG("音量已设置为 SDL: {} (UI: {}%)", sdl_volume, volume_percent);
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

void audio_player::start_playback(qint64 session_id, const QAudioFormat& format)
{
    stop_playback();
    session_id_ = session_id;
    LOG_INFO("播放流程 8/14 sdl 播放器收到启动命令 会话id {}", session_id_);

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
        emit playback_error(session_id_, QString("打开音频设备失败 %1").arg(SDL_GetError()));
        return;
    }
    LOG_INFO("sdl 音频设备已打开 id {}", device_id_);
    LOG_DEBUG("sdl 设备参数 频率 {}hz 声道 {} 格式 {} 缓冲区大小 {} 字节",
              audio_spec_.freq,
              audio_spec_.channels,
              (int)audio_spec_.format,
              audio_spec_.size);

    clear_queue();

    playback_start_offset_ms_ = 0;
    bytes_processed_by_device_ = 0;
    decoder_finished_ = false;
    playback_drained_ = false;
    is_playing_ = true;

    qint64 bytes_per_second = static_cast<qint64>(audio_spec_.freq) * audio_spec_.channels * static_cast<qint64>(sizeof(qint16));
    buffer_low_water_mark_ = bytes_per_second * kBufferLowWatermarkSeconds;
    buffer_high_water_mark_ = bytes_per_second * kBufferHighWatermarkSeconds;

    low_water_mark_triggered_.store(true, std::memory_order_relaxed);
    LOG_DEBUG("sdl 缓冲区低水位线设置为 {} 字节 {} 秒 高水位线设置为 {} 字节 {} 秒",
              buffer_low_water_mark_,
              kBufferLowWatermarkSeconds,
              buffer_high_water_mark_,
              kBufferHighWatermarkSeconds);

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
        SDL_CloseAudioDevice(device_id_);
        device_id_ = 0;
        LOG_DEBUG("sdl 音频设备已关闭");
    }
    clear_queue();
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
        return;
    }

    bool queued = false;
    int waited_ms = 0;
    while (session_id == session_id_ && is_playing_.load(std::memory_order_acquire) && waited_ms <= kPacketEnqueueMaxWaitMs)
    {
        if (packet_queue_.push(packet))
        {
            queued = true;
            break;
        }
        SDL_Delay(kPacketEnqueueRetryDelayMs);
        waited_ms += kPacketEnqueueRetryDelayMs;
    }

    if (!queued)
    {
        LOG_WARN("sdl 播放队列已满，等待 {}ms 后仍无法入队，丢弃当前音频包", waited_ms);
        return;
    }

    approx_buffered_bytes_.fetch_add(packet->data.size(), std::memory_order_relaxed);
    low_water_mark_triggered_.store(false, std::memory_order_relaxed);

    qint64 current_buffer_size = approx_buffered_bytes_.load(std::memory_order_relaxed);

    if (current_buffer_size >= buffer_high_water_mark_ || packet_queue_.write_available() <= kPacketCapacity / 2)
    {
        LOG_TRACE("sdl 缓冲区水位高 {} 字节，发出高水位信号", current_buffer_size);
        emit buffer_level_high(session_id_);
    }
}

void audio_player::handle_seek(qint64 session_id, qint64 actual_seek_ms)
{
    if (session_id != session_id_ || device_id_ == 0)
    {
        return;
    }

    SDL_LockAudioDevice(device_id_);
    clear_queue();
    playback_start_offset_ms_ = actual_seek_ms;
    bytes_processed_by_device_ = 0;
    decoder_finished_ = false;
    playback_drained_ = false;
    low_water_mark_triggered_.store(true, std::memory_order_relaxed);
    SDL_UnlockAudioDevice(device_id_);

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

    if (!is_playing_)
    {
        return;
    }

    Uint8* stream_ptr = stream;
    int bytes_to_fill = len;
    int current_sdl_volume = volume_.load(std::memory_order_relaxed);

    while (bytes_to_fill > 0)
    {
        if (!current_packet_ && !packet_queue_.pop(current_packet_))
        {
            break;
        }

        size_t bytes_remaining = current_packet_->data.size() - current_packet_->bytes_played;
        size_t bytes_to_copy = std::min(static_cast<size_t>(bytes_to_fill), bytes_remaining);

        if (bytes_to_copy > 0)
        {
            SDL_MixAudioFormat(stream_ptr,
                               current_packet_->data.data() + current_packet_->bytes_played,
                               audio_spec_.format,
                               static_cast<Uint32>(bytes_to_copy),
                               current_sdl_volume);

            stream_ptr += bytes_to_copy;
            bytes_to_fill -= static_cast<int>(bytes_to_copy);
            current_packet_->bytes_played += bytes_to_copy;

            approx_buffered_bytes_.fetch_sub(bytes_to_copy, std::memory_order_relaxed);
            bytes_processed_by_device_.fetch_add(bytes_to_copy, std::memory_order_relaxed);
        }

        if (current_packet_->bytes_played >= current_packet_->data.size())
        {
            // 回调只转移所有权；最终释放由播放器线程处理。
            if (!played_packets_.push(current_packet_))
            {
                break;
            }
            current_packet_.reset();
        }
    }

    bool queue_empty = (packet_queue_.read_available() == 0) && (!current_packet_);

    if (queue_empty && bytes_to_fill == len && decoder_finished_.load(std::memory_order_acquire))
    {
        playback_drained_ = true;
    }
}

void audio_player::update_progress_ui()
{
    if (!is_playing_ || device_id_ == 0)
    {
        return;
    }

    played_packets_.consume_all([](const std::shared_ptr<audio_packet>&) {});
    if (playback_drained_.exchange(false))
    {
        on_playback_completed_internal();
        return;
    }

    if (!low_water_mark_triggered_.load(std::memory_order_relaxed) &&
        approx_buffered_bytes_.load(std::memory_order_relaxed) < buffer_low_water_mark_ &&
        packet_queue_.read_available() < kPacketCapacity / 4)
    {
        low_water_mark_triggered_.store(true, std::memory_order_relaxed);
        emit buffer_level_low(session_id_);
    }

    qint64 bytes_per_second = static_cast<qint64>(audio_spec_.freq) * audio_spec_.channels * static_cast<qint64>(sizeof(qint16));
    if (bytes_per_second == 0)
    {
        return;
    }

    qint64 buffer_latency_ms = (static_cast<qint64>(audio_spec_.size) * 1000) / bytes_per_second;

    qint64 processed_bytes = bytes_processed_by_device_.load();
    qint64 processed_ms = (processed_bytes * 1000) / bytes_per_second;
    qint64 current_playback_ms = playback_start_offset_ms_ + processed_ms - buffer_latency_ms;
    current_playback_ms = std::max<qint64>(current_playback_ms, 0);

    LOG_TRACE("播放器时钟 start_offset_ms {} processed_ms {} latency_ms {} final_ms {}",
              playback_start_offset_ms_,
              processed_ms,
              buffer_latency_ms,
              current_playback_ms);

    emit progress_update(session_id_, current_playback_ms);
}
