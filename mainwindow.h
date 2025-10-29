#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QMap>
#include <QByteArray>
#include <QList>
#include "tray_icon.h"
#include "playlist_data.h"
#include "audio_packet.h"

enum class playback_mode : uint8_t
{
    ListLoop,
    SingleLoop,
    Shuffle,
    Sequential
};

class volume_meter;
class QCloseEvent;
class QSlider;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class spectrum_widget;
class playback_controller;
class playlist_manager;
class quick_editor;

class mainwindow : public QMainWindow
{
    Q_OBJECT

   public:
    explicit mainwindow(QWidget* parent = nullptr);
    ~mainwindow() override;

   private slots:
    void on_tree_item_double_clicked(QTreeWidgetItem* item, int column);
    void on_seek_requested();
    void on_play_pause_clicked();
    void on_next_clicked();
    void on_prev_clicked();
    void on_stop_clicked();
    void on_volume_changed(int value);
    void on_manage_playlists_action();

    void on_song_tree_context_menu_requested(const QPoint& pos);
    void on_create_playlist_action();
    void on_rename_playlist_action();
    void on_delete_playlist_action();
    void on_add_songs_action();
    void on_remove_songs_action();
    void on_sort_playlist_action();
    void on_editing_finished(bool accepted, const QString& text);

    void on_playlist_added(const Playlist& new_playlist);
    void on_playlist_removed(const QString& playlist_id);
    void on_playlist_renamed(const QString& playlist_id);
    void on_songs_changed(const QString& playlist_id);

    void on_playback_mode_clicked();

    void update_track_info(qint64 duration_ms);
    void on_playback_started(const QString& file_path, const QString& file_name);
    void update_progress(qint64 current_ms, qint64 total_ms);
    void handle_playback_finished();
    void handle_playback_error(const QString& error_message);

    void on_metadata_updated(const QMap<QString, QString>& metadata);
    void on_cover_art_updated(const QByteArray& image_data);
    void on_lyrics_updated(const QList<LyricLine>& lyrics);
    void quit_application();

   protected:
    void closeEvent(QCloseEvent* event) override;

   private:
    void setup_ui();
    void setup_connections();
    void clear_playing_indicator();
    void populate_playlists_on_startup();
    void generate_shuffled_list(QTreeWidgetItem* playlist_item, int start_song_index = -1);
    void update_playback_mode_button_style();

   private:
    playback_controller* controller_ = nullptr;
    playlist_manager* playlist_manager_ = nullptr;

    QTreeWidget* song_tree_widget_ = nullptr;
    spectrum_widget* spectrum_widget_ = nullptr;

    QLabel* cover_art_label_ = nullptr;
    QLabel* lyrics_label_ = nullptr;

    QSlider* progress_slider_ = nullptr;
    volume_meter* volume_meter_ = nullptr;

    QPushButton* prev_button_ = nullptr;
    QPushButton* play_pause_button_ = nullptr;
    QPushButton* next_button_ = nullptr;
    QPushButton* stop_button_ = nullptr;
    QPushButton* shuffle_button_ = nullptr;
    QPushButton* manage_button_ = nullptr;

    QLabel* time_label_ = nullptr;

    QTreeWidgetItem* currently_playing_item_ = nullptr;
    QTreeWidgetItem* context_menu_item_ = nullptr;
    QTreeWidgetItem* clicked_song_item_ = nullptr;
    QString current_playing_file_path_;
    bool is_playing_ = false;
    bool is_paused_ = false;
    bool is_slider_pressed_ = false;
    bool is_creating_playlist_ = false;

    playback_mode current_mode_ = playback_mode::ListLoop;
    QList<int> shuffled_indices_;
    int current_shuffle_index_ = -1;
    tray_icon* tray_icon_ = nullptr;

    QList<LyricLine> current_lyrics_;
    int current_lyric_index_ = -1;
};
#endif
