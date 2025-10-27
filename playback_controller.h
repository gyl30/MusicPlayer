#ifndef PLAYBACK_CONTROLLER_H
#define PLAYBACK_CONTROLLER_H

#include <QObject>
#include <QAudioFormat>
#include <atomic>
#include <memory>
#include <QMap>
#include <QByteArray>
#include <QList>
#include "audio_packet.h"

class QThread;
class audio_decoder;
class audio_player;
class spectrum_widget;
struct audio_packet;

class playback_controller : public QObject
{
    Q_OBJECT

   public:
    explicit playback_controller(QObject* parent = nullptr);
    ~playback_controller() override;

    void set_spectrum_widget(spectrum_widget* widget);

   public slots:
    void play_file(const QString& file_path);
    void seek(qint64 position_ms);
    void stop();
    void pause_resume();
    void set_volume(int volume_percent);

   signals:
    void track_info_ready(qint64 duration_ms);
    void progress_updated(qint64 current_ms, qint64 total_ms);
    void playback_finished();
    void playback_error(const QString& error_message);
    void seek_finished(bool success);
    void playback_started(const QString& file_path, const QString& file_name);
    void seek_completed(qint64 actual_ms);
    void metadata_ready(const QMap<QString, QString>& metadata);
    void cover_art_ready(const QByteArray& image_data);
    void lyrics_updated(const QList<LyricLine>& lyrics);

   private slots:
    void on_duration_ready(qint64 session_id, qint64 duration_ms, const QAudioFormat& format);
    void on_packet_from_decoder(qint64 session_id, const std::shared_ptr<audio_packet>& packet);
    void on_decoder_seek_finished(qint64 session_id, qint64 actual_seek_ms);
    void on_decoding_error(const QString& error_message);
    void on_player_ready_for_spectrum(qint64 session_id);
    void on_player_seek_handled(qint64 session_id);
    void on_spectrum_ready_for_decoding(qint64 session_id);
    void on_player_error(const QString& error_message);
    void on_progress_update(qint64 session_id, qint64 current_ms);
    void on_playback_finished(qint64 session_id);
    void on_packet_for_spectrum(const std::shared_ptr<audio_packet>& packet);
    void on_buffer_level_low(qint64 session_id);
    void on_buffer_level_high(qint64 session_id);
    void on_metadata_ready(qint64 session_id, const QMap<QString, QString>& metadata);
    void on_cover_art_ready(qint64 session_id, const QByteArray& image_data);
    void on_lyrics_ready(qint64 session_id, const QList<LyricLine>& lyrics);

   private:
    void cleanup_player();

    QThread* decoder_thread_ = nullptr;
    audio_decoder* decoder_ = nullptr;
    QThread* player_thread_ = nullptr;
    audio_player* player_ = nullptr;
    spectrum_widget* spectrum_widget_ = nullptr;

    bool is_playing_ = false;
    bool is_media_loaded_ = false;
    qint64 total_duration_ms_ = 0;
    std::atomic<qint64> session_id_counter_{0};
    qint64 current_session_id_ = 0;
    bool is_paused_ = false;
    std::atomic<qint64> buffered_bytes_{0};
    qint64 buffer_high_water_mark_ = 0;
    bool decoder_is_waiting_ = false;
    bool is_seeking_ = false;
    qint64 pending_seek_ms_ = -1;
    qint64 seek_result_ms_ = -1;
};

#endif
