#ifndef PLAYLIST_DATA_H
#define PLAYLIST_DATA_H

#include <QString>
#include <QList>

struct Song
{
    QString file_path;
    QString file_name;
};

inline bool operator==(const Song& left, const Song& right) { return left.file_path == right.file_path && left.file_name == right.file_name; }

struct Playlist
{
    qint64 id = -1;
    QString name;
    QList<Song> songs;
};

#endif
