#ifndef PLAYER_WINDOW_H
#define PLAYER_WINDOW_H

#include <QWidget>
#include <QMap>
#include <QByteArray>
#include <QList>
#include "audio_packet.h"

class QVBoxLayout;
class QSlider;
class QLabel;
class QPushButton;
class volume_meter;
class playback_controller;
class playlist_window;

enum class playback_mode : uint8_t
{
    ListLoop,
    SingleLoop,
    Shuffle,
    Sequential
};

class player_window : public QWidget
{
    Q_OBJECT

   public:
    explicit player_window(playback_controller* controller, playlist_window* main_wnd);
    ~player_window() override;
    void set_playback_mode(playback_mode mode);

   signals:
    void next_requested();
    void previous_requested();
    void playback_mode_changed(playback_mode new_mode);
    void stop_requested();
    void lyric_status_changed(const QString& text);

   public slots:
    void update_track_info(qint64 duration_ms);
    void on_playback_started(const QString& file_path, const QString& file_name);
    void update_progress(qint64 current_ms, qint64 total_ms);
    void handle_playback_error(const QString& error_message);
    void on_metadata_updated(const QMap<QString, QString>& metadata);
    void on_cover_art_updated(const QByteArray& image_data);
    void on_lyrics_updated(const QList<LyricLine>& lyrics);
    void on_playback_stopped();
    void on_playback_finished();
    void on_playback_paused(bool is_paused);

   protected:
    void resizeEvent(QResizeEvent* event) override;

   private slots:
    void on_seek_requested();
    void on_play_pause_clicked();
    void on_next_clicked();
    void on_prev_clicked();
    void on_stop_clicked();
    void on_volume_changed(int value);
    void on_playback_mode_clicked();

   private:
    void setup_ui();
    void setup_connections();
    void update_playback_mode_button_style();
    void reset_ui();
    void set_time_text(const QString& text);
    void set_track_title(const QString& title);
    void refresh_time_label_width();
    void refresh_track_title_elision();
    [[nodiscard]] QString lyric_at_time(qint64 time_ms) const;

   private:
    playback_controller* controller_ = nullptr;

    QWidget* main_container_ = nullptr;

    QVBoxLayout* left_panel_layout_ = nullptr;

    QSlider* progress_slider_ = nullptr;
    volume_meter* volume_meter_ = nullptr;

    QPushButton* prev_button_ = nullptr;
    QPushButton* play_pause_button_ = nullptr;
    QPushButton* next_button_ = nullptr;
    QPushButton* stop_button_ = nullptr;
    QPushButton* shuffle_button_ = nullptr;

    QLabel* time_label_ = nullptr;
    QLabel* track_title_label_ = nullptr;
    QString full_track_title_ = "欢迎使用";

    bool is_paused_ = false;
    bool is_slider_pressed_ = false;

    QList<LyricLine> lyrics_;
    QString current_lyric_status_;

    playback_mode current_mode_ = playback_mode::ListLoop;
};

#endif
