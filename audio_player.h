#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <deque>
#include <QTimer>
#include <QObject>
#include <QAudioSink>
#include "audio_packet.h"

class audio_player : public QObject
{
    Q_OBJECT

   public:
    explicit audio_player(QObject* parent = nullptr);
    ~audio_player() override;

   signals:
    void progress_update(qint64 session_id, qint64 current_ms);
    void playback_finished(qint64 session_id);
    void playback_ready(qint64 session_id);
    void playback_error(const QString& error_message);
    void packet_played(const std::shared_ptr<audio_packet>& packet);

   public slots:
    void start_playback(qint64 session_id, const QAudioFormat& format, qint64 start_offset_ms = 0);
    void stop_playback();
    void enqueue_packet(qint64 session_id, const std::shared_ptr<audio_packet>& packet);
    void handle_seek(qint64 session_id, qint64 actual_seek_ms);
    void pause_feeding(qint64 session_id);
    void resume_feeding(qint64 session_id);

   private slots:
    void feed_audio_device();
    void update_progress_ui();
    void on_sink_state_changed(QAudio::State state);

   private:
    QAudioFormat format_;
    QAudioSink* audio_sink_ = nullptr;
    QIODevice* io_device_ = nullptr;
    QTimer* feed_timer_ = nullptr;
    std::deque<std::shared_ptr<audio_packet>> data_queue_;

    qint64 session_id_ = 0;
    std::atomic<bool> is_playing_{false};
    std::atomic<bool> decoder_finished_{false};
    qint64 playback_start_offset_ms_ = 0;
    QTimer* progress_timer_ = nullptr;
};

#endif
