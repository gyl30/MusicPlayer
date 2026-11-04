#ifndef PLAYLIST_DATA_H
#define PLAYLIST_DATA_H

#include <QString>
#include <QList>
#include <QStringList>

struct Song
{
    QString file_path;
    QString file_name;
    qint64 duration_ms = 0;
};

inline bool operator==(const Song& left, const Song& right) { return left.file_path == right.file_path && left.file_name == right.file_name; }

// 音乐库中的一首歌及包含它的歌单名称。
struct LibrarySong
{
    Song song;
    QStringList playlists;
};

struct Playlist
{
    qint64 id = -1;
    QString name;
    QList<Song> songs;
};

#endif
