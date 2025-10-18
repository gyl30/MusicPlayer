#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QMap>
#include "playlist_data.h"

class QCloseEvent;
class QKeyEvent;
class QEvent;
class QSlider;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QVBoxLayout;
class QPushButton;
class QLineEdit;
class QFrame;
class spectrum_widget;
class playback_controller;
class playlist_manager;
class QStatusBar;

class mainwindow : public QMainWindow
{
    Q_OBJECT

   public:
    explicit mainwindow(QWidget* parent = nullptr);
    ~mainwindow() override;

   private slots:
    void on_play_file_requested(QListWidgetItem* item);
    void on_seek_requested();
    void on_progress_slider_moved(int position);

    void on_add_new_playlist_button_clicked();
    void finish_playlist_edit();
    void on_delete_playlist_requested();
    void on_rename_playlist_requested();
    void on_add_songs_requested();
    void on_remove_songs_requested();

    void on_playlist_context_menu_requested(const QPoint& pos);
    void on_nav_button_context_menu_requested(const QPoint& pos);
    void on_playlist_button_clicked();
    void switch_to_playlist(const QString& id);

    void update_track_info(qint64 duration_ms);
    void update_progress(qint64 current_ms, qint64 total_ms);
    void handle_playback_finished();
    void handle_playback_error(const QString& error_message);
    void handle_seek_finished(bool success);
    void on_playback_started(const QString& file_path, const QString& file_name);
    void on_current_song_selection_changed(QListWidgetItem* current, QListWidgetItem* previous);
    void on_seek_completed(qint64 actual_ms);

    void rebuild_ui_from_playlists();
    void update_playlist_content(const QString& playlist_id);

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
    void clear_playlist_ui();
    void add_playlist_to_ui(const Playlist& playlist);
    void clear_playing_indicator();
    [[nodiscard]] QListWidget* current_song_list_widget() const;

   private:
    playback_controller* controller_ = nullptr;
    playlist_manager* playlist_manager_ = nullptr;

    QStackedWidget* main_stack_widget_ = nullptr;
    QWidget* player_view_widget_ = nullptr;
    QWidget* management_view_widget_ = nullptr;

    QVBoxLayout* playlist_nav_layout_ = nullptr;
    QPushButton* add_playlist_button_ = nullptr;
    QPushButton* management_button_ = nullptr;
    QLineEdit* currently_editing_ = nullptr;
    QFrame* nav_separator_ = nullptr;

    QMap<QString, QPushButton*> playlist_buttons_;
    QMap<QString, QListWidget*> playlist_widgets_;
    QString current_playlist_id_;

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

    QStatusBar* status_bar_ = nullptr;
    QLabel* file_path_label_ = nullptr;
    QListWidgetItem* currently_playing_item_ = nullptr;

    bool is_slider_pressed_ = false;
    qint64 total_duration_ms_ = 0;
    QString current_playing_file_path_;
};

#endif
