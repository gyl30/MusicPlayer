#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <QObject>
#include <QAudioSink>
#include <QTimer>
#include "audio_packet.h"
#include "thread_safe_queue.h"

class audio_player : public QObject
{
    Q_OBJECT

   public:
    explicit audio_player(QObject* parent = nullptr);
    ~audio_player() override;

   signals:
    void progress_update(qint64 current_ms);
    void spectrum_data_ready(const std::shared_ptr<audio_packet>& packet);
    void playback_finished();

   public slots:
    void start_playback(const QAudioFormat& format, qint64 start_offset_ms = 0);
    void stop_playback();
    void enqueue_packet(const std::shared_ptr<audio_packet>& packet);
    void handle_seek(qint64 actual_seek_ms);
    void pause_feeding();
    void resume_feeding();

   private slots:
    void feed_audio_device();
    void update_progress_ui();

   private:
    QAudioFormat format_;
    QAudioSink* audio_sink_ = nullptr;
    QIODevice* io_device_ = nullptr;
    QTimer* feed_timer_ = nullptr;
    safe_queue data_queue_;

    std::atomic<bool> is_playing_{false};
    std::atomic<bool> decoder_finished_{false};
    qint64 playback_start_offset_ms_ = 0;
    QTimer* progress_timer_ = nullptr;
};

#endif
