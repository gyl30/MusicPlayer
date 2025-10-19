#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QUuid>
#include <QFileInfo>
#include <algorithm>

#include "log.h"
#include "playlist_manager.h"

playlist_manager::playlist_manager(QObject* parent) : QObject(parent)
{
    const QString app_data_path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(app_data_path);
    if (!dir.exists())
    {
        dir.mkpath(".");
    }
    playlist_storage_path_ = app_data_path + "/playlists.txt";
    LOG_INFO("播放列表管理器初始化 存储路径 {}", playlist_storage_path_.toStdString());
}

void playlist_manager::load_playlists()
{
    LOG_INFO("播放列表管理器开始加载播放列表");
    playlists_.clear();

    QFile playlist_file(playlist_storage_path_);
    if (!playlist_file.exists() || !playlist_file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        LOG_WARN("未找到播放列表文件 将创建一个默认列表");
        create_new_playlist("默认列表");
        return;
    }

    QTextStream in(&playlist_file);
    Playlist current_playlist;

    auto finalize_current_playlist = [&]()
    {
        if (!current_playlist.id.isEmpty() && !current_playlist.name.isEmpty())
        {
            LOG_DEBUG("完成加载播放列表 id {} 名称 {}", current_playlist.id.toStdString(), current_playlist.name.toStdString());
            playlists_.insert(current_playlist.id, current_playlist);
        }
        current_playlist = Playlist();
    };

    while (!in.atEnd())
    {
        QString line = in.readLine().trimmed();
        if (line.startsWith("[PLAYLIST_ID]"))
        {
            finalize_current_playlist();
            current_playlist.id = line.mid(13);
        }
        else if (line.startsWith("[PLAYLIST_NAME]"))
        {
            current_playlist.name = line.mid(15);
        }
        else if (!line.isEmpty())
        {
            QFileInfo file_info(line);
            if (file_info.exists() && file_info.isFile())
            {
                current_playlist.songs.append({line, file_info.fileName()});
            }
            else
            {
                LOG_WARN("播放列表中的文件未找到 跳过 {}", line.toStdString());
            }
        }
    }
    finalize_current_playlist();

    if (playlists_.isEmpty())
    {
        LOG_WARN("加载后未发现有效播放列表 将创建一个默认列表");
        create_new_playlist("默认列表");
    }
    LOG_INFO("播放列表管理器完成加载播放列表");
}

void playlist_manager::save_playlists()
{
    LOG_INFO("播放列表管理器开始将播放列表保存至 {}", playlist_storage_path_.toStdString());
    QFile playlist_file(playlist_storage_path_);
    if (!playlist_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        LOG_ERROR("打开播放列表文件用于写入失败 {}", playlist_storage_path_.toStdString());
        return;
    }

    QTextStream out(&playlist_file);
    for (const auto& playlist : std::as_const(playlists_))
    {
        out << "[PLAYLIST_ID]" << playlist.id << "\n";
        out << "[PLAYLIST_NAME]" << playlist.name << "\n";
        for (const auto& song : playlist.songs)
        {
            out << song.filePath << "\n";
        }
    }
    LOG_INFO("播放列表管理器完成保存播放列表");
}

QList<Playlist> playlist_manager::get_all_playlists() const { return playlists_.values(); }

Playlist playlist_manager::get_playlist_by_id(const QString& id) const { return playlists_.value(id); }

void playlist_manager::create_new_playlist(const QString& name)
{
    LOG_INFO("创建新播放列表 名称 {}", name.toStdString());
    Playlist new_playlist;
    new_playlist.id = QUuid::createUuid().toString();
    new_playlist.name = name;
    playlists_.insert(new_playlist.id, new_playlist);
    emit playlist_added(new_playlist);
}

void playlist_manager::delete_playlist(const QString& id)
{
    if (!playlists_.contains(id))
    {
        LOG_WARN("请求删除不存在的播放列表 id {}", id.toStdString());
        return;
    }

    if (playlists_.count() <= 1)
    {
        LOG_WARN("试图删除最后一个播放列表 操作不允许");
        return;
    }

    LOG_INFO("删除播放列表 id {}", id.toStdString());
    playlists_.remove(id);
    emit playlist_removed(id);
}

void playlist_manager::add_songs_to_playlist(const QString& playlist_id, const QStringList& file_paths)
{
    if (!playlists_.contains(playlist_id))
    {
        LOG_WARN("试图向不存在的播放列表添加歌曲 id {}", playlist_id.toStdString());
        return;
    }
    LOG_INFO("向播放列表id {} 添加 {} 首歌曲", playlist_id.toStdString(), file_paths.count());

    int songs_added = 0;
    for (const QString& path : file_paths)
    {
        playlists_[playlist_id].songs.append({path, QFileInfo(path).fileName()});
        songs_added++;
    }

    if (songs_added > 0)
    {
        emit songs_changed_in_playlist(playlist_id);
    }
}

void playlist_manager::remove_songs_from_playlist(const QString& playlist_id, const QList<int>& song_indices)
{
    if (!playlists_.contains(playlist_id))
    {
        LOG_WARN("试图从不存在的播放列表移除歌曲 id {}", playlist_id.toStdString());
        return;
    }
    LOG_INFO("从播放列表id {} 移除 {} 首歌曲", playlist_id.toStdString(), song_indices.count());

    QList<int> sorted_indices = song_indices;
    std::sort(sorted_indices.begin(), sorted_indices.end(), std::greater<>());

    for (int index : sorted_indices)
    {
        if (index >= 0 && index < playlists_[playlist_id].songs.count())
        {
            playlists_[playlist_id].songs.removeAt(index);
        }
    }
    emit songs_changed_in_playlist(playlist_id);
}

void playlist_manager::rename_playlist(const QString& id, const QString& new_name)
{
    if (!playlists_.contains(id))
    {
        LOG_WARN("试图重命名不存在的播放列表 id {}", id.toStdString());
        return;
    }
    if (new_name.isEmpty())
    {
        LOG_WARN("试图使用空名称重命名播放列表");
        return;
    }
    LOG_INFO("将播放列表id {} 重命名为 {}", id.toStdString(), new_name.toStdString());
    playlists_[id].name = new_name;
    emit playlist_renamed(id);
}
