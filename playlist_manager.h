#ifndef PLAYLIST_MANAGER_H
#define PLAYLIST_MANAGER_H

#include <QObject>
#include <QMap>
#include "playlist_data.h"

class database_manager;

class playlist_manager : public QObject
{
    Q_OBJECT

   public:
    explicit playlist_manager(QObject* parent = nullptr);
    ~playlist_manager() override;

    void initialize_and_load();

    [[nodiscard]] QList<Playlist> get_all_playlists() const;
    [[nodiscard]] Playlist get_playlist_by_id(qint64 id) const;
    void increment_play_count(const QString& file_path);

   public slots:
    void create_new_playlist(const QString& name);
    void delete_playlist(qint64 id);
    void add_songs_to_playlist(qint64 playlist_id, const QStringList& file_paths);
    void remove_songs_from_playlist(qint64 playlist_id, const QList<int>& song_indices);
    void rename_playlist(qint64 id, const QString& new_name);
    void sort_playlist(qint64 id);
    void apply_changes_from_dialog(const QMap<qint64, Playlist>& temp_playlists);

   signals:
    void playlist_added(const Playlist& new_playlist);
    void playlist_removed(qint64 playlist_id);
    void playlist_renamed(qint64 playlist_id);
    void songs_changed_in_playlist(qint64 playlist_id);

   private:
    void perform_migration();
    database_manager* db_manager_ = nullptr;
};

#endif
