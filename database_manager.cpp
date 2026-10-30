#include <QStandardPaths>
#include <QDir>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QFileInfo>
#include <QtGlobal>
#include <QSet>
#include "scoped_exit.h"
#include "log.h"
#include "database_manager.h"

database_manager::database_manager(QObject* parent) : QObject(parent) {}

database_manager::~database_manager()
{
    const QString connection_name = db_.connectionName();
    if (db_.isOpen())
    {
        db_.close();
    }
    db_ = QSqlDatabase();
    if (!connection_name.isEmpty())
    {
        QSqlDatabase::removeDatabase(connection_name);
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

static bool ensure_songs_duration_column()
{
    QSqlQuery query;
    if (!query.exec("PRAGMA table_info(Songs)"))
    {
        LOG_ERROR("读取 Songs 表结构失败 {}", query.lastError().text().toStdString());
        return false;
    }

    while (query.next())
    {
        if (query.value(1).toString() == QStringLiteral("duration_ms"))
        {
            return true;
        }
    }

    QSqlQuery alter_query;
    if (!alter_query.exec("ALTER TABLE Songs ADD COLUMN duration_ms INTEGER"))
    {
        LOG_ERROR("添加 Songs.duration_ms 列失败 {}", alter_query.lastError().text().toStdString());
        return false;
    }
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
    success &= ensure_songs_duration_column();

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

bool database_manager::delete_playlist(qint64 playlist_id)
{
    (void)this;
    QSqlQuery query;
    query.prepare("DELETE FROM Playlists WHERE playlist_id = :id");
    query.bindValue(":id", playlist_id);
    if (!query.exec())
    {
        LOG_ERROR("删除播放列表 ID {} 失败 {}", playlist_id, query.lastError().text().toStdString());
        return false;
    }
    return query.numRowsAffected() == 1;
}

bool database_manager::rename_playlist(qint64 playlist_id, const QString& new_name)
{
    (void)this;
    QSqlQuery query;
    query.prepare("UPDATE Playlists SET name = :name WHERE playlist_id = :id");
    query.bindValue(":name", new_name);
    query.bindValue(":id", playlist_id);
    if (!query.exec())
    {
        LOG_ERROR("重命名播放列表 ID {} 失败 {}", playlist_id, query.lastError().text().toStdString());
        return false;
    }
    return query.numRowsAffected() == 1;
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
        SELECT s.song_id, s.file_path, s.file_name, s.duration_ms
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
            song.file_path = query_songs.value(1).toString();
            song.file_name = query_songs.value(2).toString();
            const QVariant duration_value = query_songs.value(3);
            song.duration_ms = duration_value.isNull() ? 0 : qMax<qint64>(0, duration_value.toLongLong());
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

bool database_manager::add_songs_to_playlist(qint64 playlist_id, const QStringList& file_paths)
{
    if (!db_.transaction())
    {
        LOG_ERROR("开启数据库事务失败");
        return false;
    }

    QSqlQuery query_max_pos;
    query_max_pos.prepare("SELECT MAX(position) FROM PlaylistSongs WHERE playlist_id = :id");
    query_max_pos.bindValue(":id", playlist_id);

    if (!query_max_pos.exec() || !query_max_pos.next())
    {
        LOG_ERROR("读取歌曲位置失败 {}", query_max_pos.lastError().text().toStdString());
        db_.rollback();
        return false;
    }
    int current_max_pos = query_max_pos.value(0).isNull() ? -1 : query_max_pos.value(0).toInt();

    QSqlQuery query_insert;
    query_insert.prepare("INSERT OR IGNORE INTO PlaylistSongs (playlist_id, song_id, position) VALUES (:pid, :sid, :pos)");

    for (const QString& path : file_paths)
    {
        qint64 song_id = get_or_create_song_id(path);
        if (song_id == -1)
        {
            db_.rollback();
            return false;
        }
        query_insert.bindValue(":pid", playlist_id);
        query_insert.bindValue(":sid", song_id);
        query_insert.bindValue(":pos", current_max_pos + 1);
        if (!query_insert.exec())
        {
            LOG_ERROR("添加歌曲 {} 到播放列表 ID {} 失败 {}", path.toStdString(), playlist_id, query_insert.lastError().text().toStdString());
            db_.rollback();
            return false;
        }
        current_max_pos += query_insert.numRowsAffected();
    }

    if (!db_.commit())
    {
        LOG_ERROR("提交数据库事务失败");
        db_.rollback();
        return false;
    }
    return true;
}

bool database_manager::save_playlists(const QMap<qint64, Playlist>& playlists, QList<qint64>& changed_ids)
{
    changed_ids.clear();
    if (!db_.transaction())
    {
        LOG_ERROR("开启数据库事务失败 {}", db_.lastError().text().toStdString());
        return false;
    }
    auto rollback = make_scoped_exit([&]
    {
        db_.rollback();
        changed_ids.clear();
    });

    for (auto it = playlists.constBegin(); it != playlists.constEnd(); ++it)
    {
        const qint64 playlist_id = it.key();
        QSqlQuery query(db_);
        query.prepare("SELECT playlist_id FROM Playlists WHERE playlist_id = :id");
        query.bindValue(":id", playlist_id);
        if (!query.exec() || !query.next())
        {
            LOG_ERROR("无法读取播放列表 {} {}", playlist_id, query.lastError().text().toStdString());
            return false;
        }

        struct SongLink
        {
            qint64 song_id;
            int position;
        };
        QMap<QString, SongLink> existing;
        query.prepare("SELECT s.file_path, ps.song_id, ps.position FROM PlaylistSongs ps "
                      "JOIN Songs s ON s.song_id = ps.song_id WHERE ps.playlist_id = :id");
        query.bindValue(":id", playlist_id);
        if (!query.exec())
        {
            LOG_ERROR("读取播放列表歌曲失败 {}", query.lastError().text().toStdString());
            return false;
        }
        while (query.next())
        {
            existing.insert(query.value(0).toString(), {query.value(1).toLongLong(), query.value(2).toInt()});
        }

        QSet<QString> desired_paths;
        for (const auto& song : it->songs)
        {
            desired_paths.insert(song.file_path);
        }
        bool changed = false;
        for (auto link = existing.constBegin(); link != existing.constEnd(); ++link)
        {
            if (desired_paths.contains(link.key()))
            {
                continue;
            }
            query.prepare("DELETE FROM PlaylistSongs WHERE playlist_id = :pid AND song_id = :sid");
            query.bindValue(":pid", playlist_id);
            query.bindValue(":sid", link->song_id);
            if (!query.exec())
            {
                LOG_ERROR("移除播放列表歌曲失败 {}", query.lastError().text().toStdString());
                return false;
            }
            changed = true;
        }

        for (int position = 0; position < it->songs.size(); ++position)
        {
            const QString& path = it->songs[position].file_path;
            const auto link = existing.constFind(path);
            if (link != existing.constEnd() && link->position == position)
            {
                continue;
            }
            const qint64 song_id = link != existing.constEnd() ? link->song_id : get_or_create_song_id(path);
            if (song_id == -1)
            {
                return false;
            }
            if (link == existing.constEnd())
            {
                query.prepare("INSERT INTO PlaylistSongs (playlist_id, song_id, position) VALUES (:pid, :sid, :pos)");
            }
            else
            {
                query.prepare("UPDATE PlaylistSongs SET position = :pos WHERE playlist_id = :pid AND song_id = :sid");
            }
            query.bindValue(":pid", playlist_id);
            query.bindValue(":sid", song_id);
            query.bindValue(":pos", position);
            if (!query.exec())
            {
                LOG_ERROR("保存播放列表歌曲失败 {}", query.lastError().text().toStdString());
                return false;
            }
            changed = true;
        }
        if (changed)
        {
            changed_ids.append(playlist_id);
        }
    }

    if (!db_.commit())
    {
        LOG_ERROR("提交播放列表修改失败 {}", db_.lastError().text().toStdString());
        return false;
    }
    rollback.cancel();
    return true;
}

QStringList database_manager::pending_duration_paths()
{
    QSqlQuery query(db_);
    if (!query.exec("SELECT DISTINCT s.file_path FROM PlaylistSongs ps JOIN Songs s ON s.song_id = ps.song_id "
                    "WHERE s.duration_ms IS NULL"))
    {
        LOG_ERROR("读取待探测歌曲失败 {}", query.lastError().text().toStdString());
        return {};
    }
    QStringList paths;
    while (query.next())
    {
        paths.append(query.value(0).toString());
    }
    return paths;
}

bool database_manager::update_durations(const QMap<QString, qint64>& durations, QMap<QString, qint64>& updated)
{
    updated.clear();
    if (!db_.transaction())
    {
        LOG_ERROR("开启时长更新事务失败 {}", db_.lastError().text().toStdString());
        return false;
    }
    auto rollback = make_scoped_exit([&]
    {
        db_.rollback();
        updated.clear();
    });
    QSqlQuery query(db_);
    // 已移出所有歌单或已完成探测的记录，不再接受排队中的旧结果。
    query.prepare("UPDATE Songs SET duration_ms = :duration WHERE file_path = :path AND duration_ms IS NULL "
                  "AND EXISTS (SELECT 1 FROM PlaylistSongs ps WHERE ps.song_id = Songs.song_id)");
    for (auto it = durations.constBegin(); it != durations.constEnd(); ++it)
    {
        query.bindValue(":path", it.key());
        query.bindValue(":duration", it.value());
        if (!query.exec())
        {
            LOG_ERROR("更新歌曲时长失败 {}", query.lastError().text().toStdString());
            return false;
        }
        if (query.numRowsAffected() > 0)
        {
            updated.insert(it.key(), it.value());
        }
    }
    if (!db_.commit())
    {
        LOG_ERROR("提交时长更新失败 {}", db_.lastError().text().toStdString());
        return false;
    }
    rollback.cancel();
    return true;
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
