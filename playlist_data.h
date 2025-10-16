#ifndef PLAYLIST_DATA_H
#define PLAYLIST_DATA_H

#include <QString>
#include <QList>

struct Song
{
    QString filePath;
    QString fileName;
};

struct Playlist
{
    QString id;
    QString name;
    QList<Song> songs;
};

#endif
