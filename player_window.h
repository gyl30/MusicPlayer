#ifndef PLAYER_WINDOW_H
#define PLAYER_WINDOW_H

#include <QWidget>
#include <QMap>
#include <QByteArray>
#include <QList>
#include <QPropertyAnimation>
#include "audio_packet.h"
#include "playlist_data.h"

class QListWidget;
class QListWidgetItem;
class QVBoxLayout;
class QSlider;
class QLabel;
class QPushButton;
class volume_meter;
class spectrum_widget;
class playback_controller;

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
    explicit player_window(playback_controller* controller, QWidget* parent = nullptr);
    ~player_window() override;

   signals:
    void next_requested();
    void previous_requested();
    void playback_mode_changed(playback_mode new_mode);
    void stop_requested();

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
    void update_media_display_layout();
    void reset_ui();

   private:
    playback_controller* controller_ = nullptr;

    spectrum_widget* spectrum_widget_ = nullptr;

    QLabel* cover_art_label_ = nullptr;
    QListWidget* lyrics_list_widget_ = nullptr;
    QWidget* lyrics_and_cover_container_ = nullptr;
    QVBoxLayout* left_panel_layout_ = nullptr;

    QSlider* progress_slider_ = nullptr;
    volume_meter* volume_meter_ = nullptr;

    QPushButton* prev_button_ = nullptr;
    QPushButton* play_pause_button_ = nullptr;
    QPushButton* next_button_ = nullptr;
    QPushButton* stop_button_ = nullptr;
    QPushButton* shuffle_button_ = nullptr;
    QPushButton* manage_button_ = nullptr;

    QLabel* time_label_ = nullptr;
    QLabel* track_title_label_ = nullptr;

    bool is_paused_ = false;
    bool is_slider_pressed_ = false;
    bool has_cover_art_ = false;
    bool has_lyrics_ = false;

    playback_mode current_mode_ = playback_mode::ListLoop;
    QList<LyricLine> current_lyrics_;
    int current_lyric_index_ = -1;

    QPropertyAnimation* lyrics_scroll_animation_ = nullptr;
};

#endif
