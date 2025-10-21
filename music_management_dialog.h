#ifndef MUSIC_MANAGEMENT_DIALOG_H
#define MUSIC_MANAGEMENT_DIALOG_H

#include <QDialog>
#include <QMap>
#include "playlist_data.h"

class QListWidget;
class QPushButton;
class playlist_manager;

class music_management_dialog : public QDialog
{
    Q_OBJECT

   public:
    explicit music_management_dialog(playlist_manager* manager, QWidget* parent = nullptr);
    ~music_management_dialog() override = default;

   private slots:
    void on_source_playlist_selected();
    void on_dest_playlist_selected();
    void on_copy_button_clicked();
    void on_delete_button_clicked();
    void on_done_button_clicked();

   private:
    void setup_ui();
    void setup_connections();
    void populate_playlist_widgets();
    void update_songs_list(QListWidget* songs_list_widget, QListWidget* playlists_list_widget, bool is_source_list);
    void load_initial_data();

    playlist_manager* playlist_manager_ = nullptr;
    QMap<QString, Playlist> temp_playlists_;

    QListWidget* source_playlists_list_ = nullptr;
    QListWidget* source_songs_list_ = nullptr;
    QListWidget* dest_playlists_list_ = nullptr;
    QListWidget* dest_songs_list_ = nullptr;

    QPushButton* copy_button_ = nullptr;
    QPushButton* delete_button_ = nullptr;
    QPushButton* done_button_ = nullptr;
};

#endif
