#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <atomic>
#include <memory>
#include <QObject>
#include <QTimer>
#include <QAudioFormat>
#include <SDL.h>
#include <boost/lockfree/spsc_queue.hpp>

#include "audio_packet.h"

class audio_player : public QObject
{
    Q_OBJECT

   public:
    explicit audio_player(QObject* parent = nullptr);
    ~audio_player() override;

    // 取消可能正在等待空位的入队，允许控制线程随后等待解码线程退出。
    void request_stop() { is_playing_.store(false); }

   signals:
    void progress_update(qint64 session_id, qint64 current_ms);
    void playback_finished(qint64 session_id);
    void playback_ready(qint64 session_id);
    void playback_error(qint64 session_id, const QString& error_message);
    void packet_played(const std::shared_ptr<audio_packet>& packet);
    void seek_handled(qint64 session_id);
    void buffer_level_low(qint64 session_id);
    void buffer_level_high(qint64 session_id);

   public slots:
    void start_playback(qint64 session_id, const QAudioFormat& format);
    void stop_playback();
    void enqueue_packet(qint64 session_id, const std::shared_ptr<audio_packet>& packet);
    void handle_seek(qint64 session_id, qint64 actual_seek_ms);
    void pause_feeding(qint64 session_id) const;
    void resume_feeding(qint64 session_id) const;
    void set_volume(int volume_percent);

   private:
    void fill_audio_buffer(Uint8* stream, int len);
    static void audio_callback(void* userdata, Uint8* stream, int len);
    void clear_queue();

   private slots:
    void update_progress_ui();
    void on_playback_completed_internal();

   private:
    static constexpr size_t kPacketCapacity = 1024;
    SDL_AudioDeviceID device_id_ = 0;
    SDL_AudioSpec audio_spec_;

    boost::lockfree::spsc_queue<std::shared_ptr<audio_packet>, boost::lockfree::capacity<kPacketCapacity>> packet_queue_;
    boost::lockfree::spsc_queue<std::shared_ptr<audio_packet>, boost::lockfree::capacity<kPacketCapacity>> played_packets_;
    std::shared_ptr<audio_packet> current_packet_ = nullptr;
    std::atomic<int64_t> approx_buffered_bytes_{0};

    qint64 session_id_ = 0;
    std::atomic<bool> is_playing_{false};
    std::atomic<bool> decoder_finished_{false};
    std::atomic<bool> playback_drained_{false};
    qint64 playback_start_offset_ms_ = 0;
    std::atomic<qint64> bytes_processed_by_device_{0};

    QTimer* progress_timer_ = nullptr;

    qint64 buffer_low_water_mark_ = 0;
    qint64 buffer_high_water_mark_ = 0;
    std::atomic<bool> low_water_mark_triggered_{false};

    std::atomic<int> volume_{128};
};

#endif
