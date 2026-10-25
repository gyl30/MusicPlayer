#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
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
    bool delete_playlist(qint64 playlist_id);
    bool rename_playlist(qint64 playlist_id, const QString& new_name);
    QList<Playlist> get_all_playlists_with_song_counts();
    Playlist get_playlist_with_songs(qint64 playlist_id);

    bool add_songs_to_playlist(qint64 playlist_id, const QStringList& file_paths);
    bool save_playlists(const QMap<qint64, Playlist>& playlists, QList<qint64>& changed_ids);

    void increment_play_count(const QString& file_path);

   private:
    bool open_database();
    qint64 get_or_create_song_id(const QString& file_path);

   private:
    QSqlDatabase db_;
};

#endif
