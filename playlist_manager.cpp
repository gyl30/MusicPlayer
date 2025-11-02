#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <algorithm>
#include <QCollator>
#include <QStandardPaths>
#include <QDir>
#include "log.h"
#include "playlist_manager.h"
#include "database_manager.h"

playlist_manager::playlist_manager(QObject* parent) : QObject(parent) { db_manager_ = new database_manager(this); }

playlist_manager::~playlist_manager() = default;

void playlist_manager::initialize_and_load()
{
    if (!db_manager_->initialize())
    {
        LOG_ERROR("数据库管理器初始化失败！");
        return;
    }
    perform_migration();
}

void playlist_manager::perform_migration()
{
    const QString app_data_path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString old_playlist_path = app_data_path + "/playlists.txt";
    const QString backup_playlist_path = app_data_path + "/playlists.txt.bak";

    QFile old_file(old_playlist_path);
    if (!old_file.exists())
    {
        if (get_all_playlists().isEmpty())
        {
            LOG_WARN("未找到播放列表，将创建一个默认列表");
            create_new_playlist("默认列表");
        }
        return;
    }

    LOG_INFO("检测到旧的 playlists.txt 文件 开始迁移到数据库");

    if (!old_file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        LOG_ERROR("无法打开旧的播放列表文件进行迁移");
        return;
    }

    QTextStream in(&old_file);
    Playlist current_playlist;
    QString current_playlist_name;

    auto finalize_current_playlist = [&]()
    {
        if (!current_playlist_name.isEmpty())
        {
            LOG_DEBUG("正在迁移播放列表 {}", current_playlist_name.toStdString());
            Playlist new_db_playlist = db_manager_->create_playlist(current_playlist_name);
            if (new_db_playlist.id != -1)
            {
                QStringList paths;
                for (const auto& song : current_playlist.songs)
                {
                    paths.append(song.file_path);
                }
                db_manager_->add_songs_to_playlist(new_db_playlist.id, paths);
            }
        }
        current_playlist = Playlist();
        current_playlist_name.clear();
    };

    while (!in.atEnd())
    {
        QString line = in.readLine().trimmed();
        if (line.startsWith("[PLAYLIST_ID]"))
        {
            finalize_current_playlist();
        }
        else if (line.startsWith("[PLAYLIST_NAME]"))
        {
            current_playlist_name = line.mid(15);
        }
        else if (!line.isEmpty())
        {
            QFileInfo file_info(line);
            if (file_info.exists() && file_info.isFile())
            {
                current_playlist.songs.append({line, file_info.fileName()});
            }
        }
    }
    finalize_current_playlist();
    old_file.close();

    if (old_file.rename(backup_playlist_path))
    {
        LOG_INFO("旧的播放列表文件已成功备份到 {}", backup_playlist_path.toStdString());
    }
    else
    {
        LOG_ERROR("备份旧的播放列表文件失败！");
    }

    LOG_INFO("数据迁移完成。");
}

QList<Playlist> playlist_manager::get_all_playlists() const { return db_manager_->get_all_playlists_with_song_counts(); }

Playlist playlist_manager::get_playlist_by_id(qint64 id) const { return db_manager_->get_playlist_with_songs(id); }

void playlist_manager::increment_play_count(const QString& file_path) { db_manager_->increment_play_count(file_path); }

void playlist_manager::create_new_playlist(const QString& name)
{
    LOG_INFO("创建新播放列表 名称 {}", name.toStdString());
    Playlist new_playlist = db_manager_->create_playlist(name);
    if (new_playlist.id != -1)
    {
        emit playlist_added(new_playlist);
    }
}

void playlist_manager::delete_playlist(qint64 id)
{
    LOG_INFO("删除播放列表 id {}", id);
    if (db_manager_->get_all_playlists_with_song_counts().count() <= 1)
    {
        LOG_WARN("试图删除最后一个播放列表 操作不允许");
        return;
    }
    db_manager_->delete_playlist(id);
    emit playlist_removed(id);
}

void playlist_manager::add_songs_to_playlist(qint64 playlist_id, const QStringList& file_paths)
{
    LOG_INFO("向播放列表id {} 添加 {} 首歌曲", playlist_id, file_paths.count());
    db_manager_->add_songs_to_playlist(playlist_id, file_paths);
    emit songs_changed_in_playlist(playlist_id);
}

void playlist_manager::remove_songs_from_playlist(qint64 playlist_id, const QList<int>& song_indices)
{
    LOG_INFO("从播放列表id {} 移除 {} 首歌曲", playlist_id, song_indices.count());
    db_manager_->remove_songs_from_playlist(playlist_id, song_indices);
    emit songs_changed_in_playlist(playlist_id);
}

void playlist_manager::rename_playlist(qint64 id, const QString& new_name)
{
    if (new_name.isEmpty())
    {
        LOG_WARN("试图使用空名称重命名播放列表");
        return;
    }
    LOG_INFO("将播放列表id {} 重命名为 {}", id, new_name.toStdString());
    db_manager_->rename_playlist(id, new_name);
    emit playlist_renamed(id);
}

void playlist_manager::sort_playlist(qint64 id)
{
    LOG_INFO("排序播放列表 id {}", id);
    Playlist playlist = db_manager_->get_playlist_with_songs(id);

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);

    std::sort(playlist.songs.begin(),
              playlist.songs.end(),
              [&collator](const Song& s1, const Song& s2) { return collator.compare(s1.file_name, s2.file_name) < 0; });

    db_manager_->update_song_order_in_playlist(id, playlist.songs);
    emit songs_changed_in_playlist(id);
}

void playlist_manager::apply_changes_from_dialog(const QMap<qint64, Playlist>& temp_playlists)
{
    for (auto it = temp_playlists.constBegin(); it != temp_playlists.constEnd(); ++it)
    {
        qint64 playlist_id = it.key();
        const Playlist& temp_playlist = it.value();

        Playlist original_playlist = get_playlist_by_id(playlist_id);

        if (original_playlist.songs == temp_playlist.songs)
        {
            continue;
        }
        LOG_DEBUG("检测到播放列表 {} 的歌曲已变更 正在应用更新", temp_playlist.name.toStdString());
        QList<int> all_indices;
        for (int i = 0; i < original_playlist.songs.count(); ++i)
        {
            all_indices.append(i);
        }
        if (!all_indices.isEmpty())
        {
            remove_songs_from_playlist(playlist_id, all_indices);
        }

        QStringList new_paths;
        for (const auto& song : temp_playlist.songs)
        {
            new_paths.append(song.file_path);
        }
        if (!new_paths.isEmpty())
        {
            add_songs_to_playlist(playlist_id, new_paths);
        }
    }
}
