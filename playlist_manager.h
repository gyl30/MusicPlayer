#ifndef PLAYLIST_MANAGER_H
#define PLAYLIST_MANAGER_H

#include <QObject>
#include <QMap>
#include "playlist_data.h"

class playlist_manager : public QObject
{
    Q_OBJECT

   public:
    explicit playlist_manager(QObject* parent = nullptr);

    void load_playlists();
    void save_playlists();

    [[nodiscard]] QList<Playlist> get_all_playlists() const;
    [[nodiscard]] Playlist get_playlist_by_id(const QString& id) const;

   public slots:
    void create_new_playlist(const QString& name);
    void delete_playlist(const QString& id);
    void add_songs_to_playlist(const QString& playlist_id, const QStringList& file_paths);
    void remove_songs_from_playlist(const QString& playlist_id, const QList<int>& song_indices);
    void rename_playlist(const QString& id, const QString& new_name);
    void sort_playlist(const QString& id);

   signals:
    void playlist_added(const Playlist& new_playlist);
    void playlist_removed(const QString& playlist_id);
    void playlist_renamed(const QString& playlist_id);
    void songs_changed_in_playlist(const QString& playlist_id);

   private:
    QMap<QString, Playlist> playlists_;
    QString playlist_storage_path_;
};

#endif
