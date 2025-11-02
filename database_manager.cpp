#include <QStandardPaths>
#include <QDir>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QFileInfo>
#include <algorithm>
#include "log.h"
#include "database_manager.h"

database_manager::database_manager(QObject* parent) : QObject(parent) {}

database_manager::~database_manager()
{
    if (db_.isOpen())
    {
        db_.close();
    }
}

bool database_manager::open_database()
{
    const QString app_data_path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(app_data_path);
    if (!dir.exists())
    {
        dir.mkpath(".");
    }
    const QString db_path = app_data_path + "/music_library.db";

    db_ = QSqlDatabase::addDatabase("QSQLITE");
    db_.setDatabaseName(db_path);

    if (!db_.open())
    {
        LOG_ERROR("无法打开数据库 {}", db_.lastError().text().toStdString());
        return false;
    }

    LOG_INFO("数据库已成功打开于 {}", db_path.toStdString());
    return true;
}

static bool create_tables()
{
    QSqlQuery query;
    bool success = true;

    if (!query.exec("PRAGMA foreign_keys = ON;"))
    {
        LOG_WARN("无法开启外键约束 {}", query.lastError().text().toStdString());
    }

    success &= query.exec(R"(
        CREATE TABLE IF NOT EXISTS Songs (
            song_id     INTEGER PRIMARY KEY AUTOINCREMENT,
            file_path   TEXT UNIQUE NOT NULL,
            file_name   TEXT NOT NULL,
            title       TEXT,
            artist      TEXT,
            album       TEXT,
            duration_ms INTEGER,
            play_count  INTEGER NOT NULL DEFAULT 0,
            rating      INTEGER CHECK(rating >= 1 AND rating <= 5),
            date_added  DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
        )
    )");
    if (!success)
    {
        LOG_ERROR("创建 Songs 表失败 {}", query.lastError().text().toStdString());
    }

    success &= query.exec(R"(
        CREATE TABLE IF NOT EXISTS Playlists (
            playlist_id INTEGER PRIMARY KEY AUTOINCREMENT,
            name        TEXT NOT NULL
        )
    )");
    if (!success)
    {
        LOG_ERROR("创建 Playlists 表失败 {}", query.lastError().text().toStdString());
    }

    success &= query.exec(R"(
        CREATE TABLE IF NOT EXISTS PlaylistSongs (
            playlist_id INTEGER,
            song_id     INTEGER,
            position    INTEGER NOT NULL,
            PRIMARY KEY (playlist_id, song_id),
            FOREIGN KEY (playlist_id) REFERENCES Playlists(playlist_id) ON DELETE CASCADE,
            FOREIGN KEY (song_id) REFERENCES Songs(song_id) ON DELETE CASCADE
        )
    )");
    if (!success)
    {
        LOG_ERROR("创建 PlaylistSongs 表失败 {}", query.lastError().text().toStdString());
    }

    return success;
}

bool database_manager::initialize()
{
    if (!open_database())
    {
        return false;
    }
    if (!create_tables())
    {
        return false;
    }
    return true;
}

Playlist database_manager::create_playlist(const QString& name)
{
    (void)this;
    Playlist new_playlist;
    new_playlist.name = name;

    QSqlQuery query;
    query.prepare("INSERT INTO Playlists (name) VALUES (:name)");
    query.bindValue(":name", name);

    if (!query.exec())
    {
        LOG_ERROR("创建播放列表 {} 失败 {}", name.toStdString(), query.lastError().text().toStdString());
        return new_playlist;
    }

    new_playlist.id = query.lastInsertId().toLongLong();
    return new_playlist;
}

void database_manager::delete_playlist(qint64 playlist_id)
{
    (void)this;
    QSqlQuery query;
    query.prepare("DELETE FROM Playlists WHERE playlist_id = :id");
    query.bindValue(":id", playlist_id);
    if (!query.exec())
    {
        LOG_ERROR("删除播放列表 ID {} 失败 {}", playlist_id, query.lastError().text().toStdString());
    }
}

void database_manager::rename_playlist(qint64 playlist_id, const QString& new_name)
{
    (void)this;
    QSqlQuery query;
    query.prepare("UPDATE Playlists SET name = :name WHERE playlist_id = :id");
    query.bindValue(":name", new_name);
    query.bindValue(":id", playlist_id);
    if (!query.exec())
    {
        LOG_ERROR("重命名播放列表 ID {} 失败 {}", playlist_id, query.lastError().text().toStdString());
    }
}

QList<Playlist> database_manager::get_all_playlists_with_song_counts()
{
    (void)this;
    QList<Playlist> playlists;
    QSqlQuery query(R"(
        SELECT p.playlist_id, p.name, COUNT(ps.song_id) as song_count
        FROM Playlists p
        LEFT JOIN PlaylistSongs ps ON p.playlist_id = ps.playlist_id
        GROUP BY p.playlist_id, p.name
        ORDER BY p.playlist_id
    )");

    if (!query.exec())
    {
        LOG_ERROR("获取所有播放列表失败: {}", query.lastError().text().toStdString());
        return playlists;
    }

    while (query.next())
    {
        Playlist p;
        p.id = query.value("playlist_id").toLongLong();
        p.name = query.value("name").toString();
        playlists.append(p);
    }
    return playlists;
}

Playlist database_manager::get_playlist_with_songs(qint64 playlist_id)
{
    (void)this;
    Playlist playlist;
    QSqlQuery query_playlist;
    query_playlist.prepare("SELECT name FROM Playlists WHERE playlist_id = :id");
    query_playlist.bindValue(":id", playlist_id);

    if (query_playlist.exec() && query_playlist.next())
    {
        playlist.id = playlist_id;
        playlist.name = query_playlist.value(0).toString();
    }
    else
    {
        LOG_WARN("找不到播放列表 ID {}", playlist_id);
        return playlist;
    }

    QSqlQuery query_songs;
    query_songs.prepare(R"(
        SELECT s.file_path, s.file_name
        FROM PlaylistSongs ps
        JOIN Songs s ON ps.song_id = s.song_id
        WHERE ps.playlist_id = :id
        ORDER BY ps.position
    )");
    query_songs.bindValue(":id", playlist_id);

    if (query_songs.exec())
    {
        while (query_songs.next())
        {
            Song song;
            song.file_path = query_songs.value(0).toString();
            song.file_name = query_songs.value(1).toString();
            playlist.songs.append(song);
        }
    }
    return playlist;
}

qint64 database_manager::get_or_create_song_id(const QString& file_path)
{
    (void)this;
    QSqlQuery query;
    query.prepare("SELECT song_id FROM Songs WHERE file_path = :path");
    query.bindValue(":path", file_path);

    if (query.exec() && query.next())
    {
        return query.value(0).toLongLong();
    }

    QFileInfo file_info(file_path);
    query.prepare("INSERT INTO Songs (file_path, file_name) VALUES (:path, :name)");
    query.bindValue(":path", file_path);
    query.bindValue(":name", file_info.fileName());

    if (query.exec())
    {
        return query.lastInsertId().toLongLong();
    }
    LOG_ERROR("创建歌曲条目 {} 失败 {}", file_path.toStdString(), query.lastError().text().toStdString());
    return -1;
}

void database_manager::add_songs_to_playlist(qint64 playlist_id, const QStringList& file_paths)
{
    if (!db_.transaction())
    {
        LOG_ERROR("开启数据库事务失败");
        return;
    }

    QSqlQuery query_max_pos;
    query_max_pos.prepare("SELECT MAX(position) FROM PlaylistSongs WHERE playlist_id = :id");
    query_max_pos.bindValue(":id", playlist_id);

    int current_max_pos = -1;
    if (query_max_pos.exec() && query_max_pos.next())
    {
        bool ok = false;
        int val = query_max_pos.value(0).toInt(&ok);
        if (ok)
        {
            current_max_pos = val;
        }
    }

    QSqlQuery query_insert;
    query_insert.prepare("INSERT OR IGNORE INTO PlaylistSongs (playlist_id, song_id, position) VALUES (:pid, :sid, :pos)");

    for (const QString& path : file_paths)
    {
        qint64 song_id = get_or_create_song_id(path);
        if (song_id != -1)
        {
            current_max_pos++;
            query_insert.bindValue(":pid", playlist_id);
            query_insert.bindValue(":sid", song_id);
            query_insert.bindValue(":pos", current_max_pos);
            if (!query_insert.exec())
            {
                LOG_ERROR("添加歌曲 {} 到播放列表 ID {} 失败 {}", path.toStdString(), playlist_id, query_insert.lastError().text().toStdString());
                db_.rollback();
                return;
            }
        }
    }

    if (!db_.commit())
    {
        LOG_ERROR("提交数据库事务失败");
        db_.rollback();
    }
}

void database_manager::remove_songs_from_playlist(qint64 playlist_id, const QList<int>& song_indices)
{
    if (!db_.transaction())
    {
        LOG_ERROR("开启数据库事务失败");
        return;
    }

    QList<qint64> song_ids_to_keep;
    QSqlQuery query_get;
    query_get.prepare(R"(
        SELECT song_id FROM PlaylistSongs
        WHERE playlist_id = :id ORDER BY position
    )");
    query_get.bindValue(":id", playlist_id);
    if (query_get.exec())
    {
        int current_pos = 0;
        QList<int> sorted_indices = song_indices;
        std::sort(sorted_indices.begin(), sorted_indices.end());

        while (query_get.next())
        {
            if (!sorted_indices.contains(current_pos))
            {
                song_ids_to_keep.append(query_get.value(0).toLongLong());
            }
            current_pos++;
        }
    }
    else
    {
        LOG_ERROR("获取播放列表歌曲失败 {}", query_get.lastError().text().toStdString());
        db_.rollback();
        return;
    }

    QSqlQuery query_delete;
    query_delete.prepare("DELETE FROM PlaylistSongs WHERE playlist_id = :id");
    query_delete.bindValue(":id", playlist_id);
    if (!query_delete.exec())
    {
        LOG_ERROR("删除旧播放列表条目失败 {}", query_delete.lastError().text().toStdString());
        db_.rollback();
        return;
    }

    QSqlQuery query_reinsert;
    query_reinsert.prepare("INSERT INTO PlaylistSongs (playlist_id, song_id, position) VALUES (:pid, :sid, :pos)");
    for (int i = 0; i < song_ids_to_keep.size(); ++i)
    {
        query_reinsert.bindValue(":pid", playlist_id);
        query_reinsert.bindValue(":sid", song_ids_to_keep[i]);
        query_reinsert.bindValue(":pos", i);
        if (!query_reinsert.exec())
        {
            LOG_ERROR("重新插入歌曲失败 {}", query_reinsert.lastError().text().toStdString());
            db_.rollback();
            return;
        }
    }

    if (!db_.commit())
    {
        LOG_ERROR("提交数据库事务失败");
        db_.rollback();
    }
}

void database_manager::update_song_order_in_playlist(qint64 playlist_id, const QList<Song>& songs)
{
    if (!db_.transaction())
    {
        LOG_ERROR("开启数据库事务失败");
        return;
    }

    QSqlQuery query_update;
    query_update.prepare("UPDATE PlaylistSongs SET position = :pos WHERE playlist_id = :pid AND song_id = :sid");

    for (int i = 0; i < songs.size(); ++i)
    {
        qint64 song_id = get_or_create_song_id(songs[i].file_path);
        if (song_id == -1)
        {
            continue;
        }
        query_update.bindValue(":pos", i);
        query_update.bindValue(":pid", playlist_id);
        query_update.bindValue(":sid", song_id);
        if (!query_update.exec())
        {
            LOG_ERROR("更新歌曲顺序失败 {}", query_update.lastError().text().toStdString());
            db_.rollback();
            return;
        }
    }

    if (!db_.commit())
    {
        LOG_ERROR("提交数据库事务失败");
        db_.rollback();
    }
}

void database_manager::increment_play_count(const QString& file_path)
{
    qint64 song_id = get_or_create_song_id(file_path);
    if (song_id == -1)
    {
        return;
    }

    QSqlQuery query;
    query.prepare("UPDATE Songs SET play_count = play_count + 1 WHERE song_id = :id");
    query.bindValue(":id", song_id);
    if (!query.exec())
    {
        LOG_WARN("更新播放次数失败 {} {}", file_path.toStdString(), query.lastError().text().toStdString());
    }
}
