#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QUuid>
#include <QFileInfo>

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
    LOG_INFO("playlist manager initialized storage path is {}", playlist_storage_path_.toStdString());
}

void playlist_manager::load_playlists()
{
    LOG_INFO("playlist manager starts loading playlists");
    playlists_.clear();

    QFile playlist_file(playlist_storage_path_);
    if (!playlist_file.exists() || !playlist_file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        LOG_WARN("playlists file not found creating a default list");
        create_new_playlist("默认列表");
        return;
    }

    QTextStream in(&playlist_file);
    Playlist current_playlist;

    auto finalize_current_playlist = [&]()
    {
        if (!current_playlist.id.isEmpty() && !current_playlist.name.isEmpty())
        {
            LOG_DEBUG("finalizing loaded playlist id {} name {}", current_playlist.id.toStdString(), current_playlist.name.toStdString());
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
                LOG_WARN("file from playlist not found skipping {}", line.toStdString());
            }
        }
    }
    finalize_current_playlist();

    if (playlists_.isEmpty())
    {
        LOG_WARN("no valid playlists found after loading creating a default list");
        create_new_playlist("默认列表");
    }
    else
    {
        emit playlists_changed();
    }
    LOG_INFO("playlist manager finished loading playlists");
}

void playlist_manager::save_playlists()
{
    LOG_INFO("playlist manager starts saving playlists to {}", playlist_storage_path_.toStdString());
    QFile playlist_file(playlist_storage_path_);
    if (!playlist_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        LOG_ERROR("open playlists file for writing failed {}", playlist_storage_path_.toStdString());
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
    LOG_INFO("playlist manager finished saving playlists");
}

QList<Playlist> playlist_manager::get_all_playlists() const { return playlists_.values(); }

Playlist playlist_manager::get_playlist_by_id(const QString& id) const { return playlists_.value(id); }

void playlist_manager::create_new_playlist(const QString& name)
{
    LOG_INFO("creating new playlist with name {}", name.toStdString());
    Playlist new_playlist;
    new_playlist.id = QUuid::createUuid().toString();
    new_playlist.name = name;
    playlists_.insert(new_playlist.id, new_playlist);
    emit playlists_changed();
}

void playlist_manager::delete_playlist(const QString& id)
{
    if (!playlists_.contains(id))
    {
        LOG_WARN("request to delete non-existent playlist id {}", id.toStdString());
        return;
    }

    if (playlists_.count() <= 1)
    {
        LOG_WARN("attempted to delete the last playlist which is not allowed");
        return;
    }

    LOG_INFO("deleting playlist id {}", id.toStdString());
    playlists_.remove(id);
    emit playlists_changed();
}

void playlist_manager::add_songs_to_playlist(const QString& playlist_id, const QStringList& file_paths)
{
    if (!playlists_.contains(playlist_id))
    {
        LOG_WARN("attempted to add songs to non-existent playlist id {}", playlist_id.toStdString());
        return;
    }
    LOG_INFO("adding {} songs to playlist id {}", file_paths.count(), playlist_id.toStdString());

    int songs_added = 0;
    for (const QString& path : file_paths)
    {
        playlists_[playlist_id].songs.append({path, QFileInfo(path).fileName()});
        songs_added++;
    }

    if (songs_added > 0)
    {
        emit playlist_content_changed(playlist_id);
    }
}

void playlist_manager::remove_songs_from_playlist(const QString& playlist_id, const QList<int>& song_indices)
{
    if (!playlists_.contains(playlist_id))
    {
        LOG_WARN("attempted to remove songs from non-existent playlist id {}", playlist_id.toStdString());
        return;
    }
    LOG_INFO("removing {} songs from playlist id {}", song_indices.count(), playlist_id.toStdString());

    QList<int> sorted_indices = song_indices;
    std::sort(sorted_indices.begin(), sorted_indices.end(), std::greater<>());

    for (int index : sorted_indices)
    {
        if (index >= 0 && index < playlists_[playlist_id].songs.count())
        {
            playlists_[playlist_id].songs.removeAt(index);
        }
    }
    emit playlist_content_changed(playlist_id);
}

void playlist_manager::rename_playlist(const QString& id, const QString& new_name)
{
    if (!playlists_.contains(id))
    {
        LOG_WARN("attempted to rename non-existent playlist id {}", id.toStdString());
        return;
    }
    if (new_name.isEmpty())
    {
        LOG_WARN("attempted to rename playlist with an empty name");
        return;
    }
    LOG_INFO("renaming playlist id {} to {}", id.toStdString(), new_name.toStdString());
    playlists_[id].name = new_name;
    emit playlists_changed();
}
