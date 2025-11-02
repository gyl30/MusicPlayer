#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <QObject>
#include <QString>
#include <QList>
#include <QSqlDatabase>
#include "playlist_data.h"

class database_manager : public QObject
{
    Q_OBJECT

   public:
    explicit database_manager(QObject* parent = nullptr);
    ~database_manager() override;

    bool initialize();

    Playlist create_playlist(const QString& name);
    void delete_playlist(qint64 playlist_id);
    void rename_playlist(qint64 playlist_id, const QString& new_name);
    QList<Playlist> get_all_playlists_with_song_counts();
    Playlist get_playlist_with_songs(qint64 playlist_id);

    void add_songs_to_playlist(qint64 playlist_id, const QStringList& file_paths);
    void remove_songs_from_playlist(qint64 playlist_id, const QList<int>& song_indices);
    void update_song_order_in_playlist(qint64 playlist_id, const QList<Song>& songs);

    void increment_play_count(const QString& file_path);

   private:
    bool open_database();
    qint64 get_or_create_song_id(const QString& file_path);

   private:
    QSqlDatabase db_;
};

#endif
