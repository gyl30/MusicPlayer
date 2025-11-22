#ifndef PLAYLIST_WINDOW_H
#define PLAYLIST_WINDOW_H

#include <QMainWindow>
#include <QList>
#include <QMap>
#include <QPoint>

#include "playlist_data.h"
#include "player_window.h"

class QTreeWidget;
class QTreeWidgetItem;
class QCloseEvent;
class QPushButton;
class QMoveEvent;
class QEvent;
class QMouseEvent;

class playback_controller;
class playlist_manager;
class quick_editor;
class tray_icon;
class player_window;

class playlist_window : public QMainWindow
{
    Q_OBJECT

   public:
    explicit playlist_window(QWidget* parent = nullptr);
    ~playlist_window() override;

   private slots:
    void on_tree_item_double_clicked(QTreeWidgetItem* item, int column);
    void on_song_tree_context_menu_requested(const QPoint& pos);
    void on_create_playlist_action();
    void on_rename_playlist_action();
    void on_delete_playlist_action();
    void on_add_songs_action();
    void on_remove_songs_action();
    void on_sort_playlist_action();
    void on_editing_finished(bool accepted, const QString& text);
    void on_manage_playlists_action();
    void on_toggle_player_window_clicked();

    void on_playlist_added(const Playlist& new_playlist);
    void on_playlist_removed(qint64 playlist_id);
    void on_playlist_renamed(qint64 playlist_id);
    void on_songs_changed(qint64 playlist_id);

    void handle_playback_finished();
    void on_playback_started(const QString& file_path, const QString& file_name);

    void on_next_requested();
    void on_previous_requested();
    void on_stop_requested();
    void on_playback_mode_changed(playback_mode new_mode);

    void quit_application();

    void on_player_request_snap(snap_side side);
    void on_player_request_detach();
    void on_player_request_resnap();

    void handle_playback_error_strategy(const QString& error_message);

   protected:
    void closeEvent(QCloseEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void setup_ui();
    void setup_connections();
    void clear_playing_indicator();
    void populate_playlists_on_startup();
    void generate_shuffled_list(QTreeWidgetItem* playlist_item, int start_song_index = -1);
    void play_first_song_in_list();
    void update_player_window_position();

   private:
    playback_controller* controller_ = nullptr;
    playlist_manager* playlist_manager_ = nullptr;
    player_window* player_window_ = nullptr;
    tray_icon* tray_icon_ = nullptr;

    QTreeWidget* song_tree_widget_ = nullptr;
    QPushButton* toggle_player_window_button_ = nullptr;

    QTreeWidgetItem* currently_playing_item_ = nullptr;
    QTreeWidgetItem* context_menu_item_ = nullptr;
    QTreeWidgetItem* clicked_song_item_ = nullptr;
    QString current_playing_file_path_;
    bool is_creating_playlist_ = false;

    playback_mode current_mode_ = playback_mode::ListLoop;
    QList<int> shuffled_indices_;
    int current_shuffle_index_ = -1;

    bool is_player_attached_ = true;
    snap_side current_snap_side_ = snap_side::bottom;

    bool is_being_dragged_by_user_ = false;
    QPoint drag_position_;
    QPoint last_player_pos_;

    int consecutive_failures_ = 0;
    const int MAX_CONSECUTIVE_FAILURES = 5;
};

#endif
