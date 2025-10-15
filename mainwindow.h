#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#pragma once

#include <QMainWindow>
#include <QAudioFormat>
#include <QTimer>
#include <QListWidgetItem>
#include <atomic>

class QCloseEvent;
class QKeyEvent;
class QEvent;
class QSlider;
class QLabel;
class QListWidget;
class QStackedWidget;
class QVBoxLayout;
class QPushButton;
class QButtonGroup;
class QLineEdit;
class QThread;
class QSplitter;
class QFrame;
class spectrum_widget;
class audio_decoder;
class audio_player;
struct audio_packet;

class mainwindow : public QMainWindow
{
    Q_OBJECT

   public:
    explicit mainwindow(QWidget* parent = nullptr);
    ~mainwindow() override;

   signals:
    void request_decoding(qint64 session_id, const QString& file_path, const QAudioFormat& target_format, qint64 initial_seek_ms);
    void request_resume_decoding();
    void request_stop_decoding();
    void request_seek(qint64 session_id, qint64 position_ms);

   private slots:
    void finish_playlist_edit();
    void on_playlist_context_menu_requested(const QPoint& pos);
    void on_nav_button_context_menu_requested(const QPoint& pos);
    void on_playlist_button_clicked(int id);
    void add_new_playlist();
    void delete_playlist(int index);

    void on_list_double_clicked(QListWidgetItem* item);
    void stop_playback();
    void on_progress_slider_moved(int position);
    void on_seek_requested();

    void on_duration_ready(qint64 session_id, qint64 duration_ms, const QAudioFormat& format);
    void on_packet_from_decoder(qint64 session_id, const std::shared_ptr<audio_packet>& packet);
    void on_seek_finished(qint64 session_id, qint64 actual_seek_ms);
    void on_decoding_error(const QString& error_message);

    void on_player_ready(qint64 session_id);
    void on_player_error(const QString& error_message);
    void on_progress_update(qint64 session_id, qint64 current_ms);
    void on_playback_finished(qint64 session_id);
    void on_packet_for_spectrum(const std::shared_ptr<audio_packet>& packet);

    void show_management_view();
    void show_player_view();
    void update_management_source_songs(QListWidgetItem* item);
    void update_management_dest_songs(QListWidgetItem* item);
    void add_selected_songs_to_playlist();
    void populate_management_view();

   protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void setup_ui();
    void setup_connections();
    void cleanup_player();
    void create_new_playlist(const QString& name, bool is_loading = false);

    void load_playlist();
    void save_playlist();

    [[nodiscard]] QListWidget* get_list_widget_by_index(int index) const;
    [[nodiscard]] QListWidget* current_song_list_widget() const;

   private:
    QStackedWidget* main_stack_widget_ = nullptr;
    QWidget* player_view_widget_ = nullptr;
    QWidget* management_view_widget_ = nullptr;

    QVBoxLayout* playlist_nav_layout_ = nullptr;
    QPushButton* add_playlist_button_ = nullptr;
    QPushButton* management_button_ = nullptr;
    QButtonGroup* playlist_button_group_ = nullptr;
    QLineEdit* currently_editing_ = nullptr;
    QFrame* nav_separator_ = nullptr;

    QStackedWidget* playlist_stack_ = nullptr;
    spectrum_widget* spectrum_widget_ = nullptr;
    QSlider* progress_slider_ = nullptr;
    QLabel* time_label_ = nullptr;

    QListWidget* mgmt_source_playlists_ = nullptr;
    QListWidget* mgmt_source_songs_ = nullptr;
    QListWidget* mgmt_dest_playlists_ = nullptr;
    QListWidget* mgmt_dest_songs_ = nullptr;
    QPushButton* add_songs_to_playlist_button_ = nullptr;
    QPushButton* finish_management_button_ = nullptr;

    QThread* decoder_thread_ = nullptr;
    audio_decoder* decoder_ = nullptr;
    QThread* player_thread_ = nullptr;
    audio_player* player_ = nullptr;

    bool is_playing_ = false;
    bool is_media_loaded_ = false;
    qint64 total_duration_ms_ = 0;
    bool is_slider_pressed_ = false;
    QString playlist_path_;
    QString current_playing_file_path_;
    std::atomic<qint64> session_id_counter_{0};
    qint64 current_session_id_ = 0;
    std::atomic<qint64> buffered_bytes_{0};
    qint64 buffer_high_water_mark_ = 0;
    bool decoder_is_waiting_ = false;
    bool is_seeking_ = false;
    qint64 pending_seek_ms_ = -1;
};

#endif
