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
class QPropertyAnimation;

class playback_controller;
class playlist_manager;
class quick_editor;
class tray_icon;
class player_window;
class lyrics_widget;

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
    void on_copy_songs_action(qint64 target_id);
    void on_move_songs_action(qint64 target_id);
    void on_cleanup_invalid_action();
    void handle_song_drop(const QPoint& position);

    void on_playlist_added(const Playlist& new_playlist);
    void on_playlist_removed(qint64 playlist_id);
    void on_playlist_renamed(qint64 playlist_id);
    void on_songs_changed(qint64 playlist_id);

    void handle_playback_finished();
    void on_playback_started(const QString& file_path, const QString& file_name);

    void on_next_requested();
    void on_previous_requested();
    void on_stop_requested();
    void on_play_requested();
    void toggle_playback();
    void seek_relative(qint64 offset_ms);
    void on_playback_mode_changed(playback_mode new_mode);

    void quit_application();

    void handle_playback_error_strategy(const QString& error_message);
    void on_file_unavailable(const QString& file_path);

   protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

   private:
    void setup_ui();
    void setup_connections();
    void set_lyrics_open(bool open);
    void update_lyrics_button();
    int lyrics_target_width() const;
    void update_resize_handles();
    void apply_theme(bool dark);
    void clear_playing_indicator();
    void populate_playlists_on_startup();
    void restore_playback_state();
    void save_playback_state();
    void generate_shuffled_list(QTreeWidgetItem* playlist_item, int start_song_index = -1);
    QTreeWidgetItem* find_song_item_by_path(const QString& file_path) const;
    void mark_restored_song_item(QTreeWidgetItem* item);
    void play_song_item(QTreeWidgetItem* item, bool increment_play_count, qint64 restore_position_ms = -1);
    bool play_first_song_in_list();
    void attach_playing_item(QTreeWidgetItem* item);
    QTreeWidgetItem* detached_neighbor(int step) const;
    enum class song_transfer
    {
        copy,
        move
    };
    QList<int> selected_song_indices(QTreeWidgetItem*& playlist_item) const;
    // position 为目标歌单中的插入位置，-1 表示追加到末尾。
    void transfer_selected_songs(qint64 target_id, song_transfer transfer, int position = -1);

   private:
    playback_controller* controller_ = nullptr;
    playlist_manager* playlist_manager_ = nullptr;
    player_window* player_window_ = nullptr;
    lyrics_widget* lyrics_view_ = nullptr;
    QWidget* lyrics_panel_ = nullptr;
    QPropertyAnimation* lyrics_animation_ = nullptr;
    QPushButton* lyrics_button_ = nullptr;
    bool lyrics_open_ = false;
    QPushButton* theme_button_ = nullptr;
    QList<QWidget*> resize_handles_;
    tray_icon* tray_icon_ = nullptr;

    QTreeWidget* song_tree_widget_ = nullptr;

    QTreeWidgetItem* currently_playing_item_ = nullptr;
    QTreeWidgetItem* context_menu_item_ = nullptr;
    QTreeWidgetItem* clicked_song_item_ = nullptr;
    QTreeWidgetItem* restored_song_item_ = nullptr;
    QString current_playing_file_path_;
    qint64 current_playing_playlist_id_ = -1;
    // 正在播放的歌曲被移出歌单后，以它原来的位置决定上一首/下一首。
    int detached_index_ = -1;
    bool is_creating_playlist_ = false;
    qint64 current_progress_ms_ = 0;
    qint64 current_duration_ms_ = 0;
    qint64 pending_restore_seek_ms_ = -1;
    qint64 last_logged_progress_ms_ = -1;
    qint64 last_saved_progress_ms_ = 0;

    playback_mode current_mode_ = playback_mode::ListLoop;
    QList<int> shuffled_indices_;
    int current_shuffle_index_ = -1;

    int consecutive_failures_ = 0;
    const int MAX_CONSECUTIVE_FAILURES = 5;
};

#endif
