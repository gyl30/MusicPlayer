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
    QString title;
    QString artist;
    QString album;
};

inline bool operator==(const Song& left, const Song& right) { return left.file_path == right.file_path && left.file_name == right.file_name; }

// 文件无法打开时的时长，区别于能打开但没有时长信息的 0。
constexpr qint64 kSongScanFailed = -1;

// 后台扫描得到的歌曲信息；标签缺失时为空字符串。
struct scanned_song
{
    qint64 duration_ms = kSongScanFailed;
    QString title;
    QString artist;
    QString album;
};

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
