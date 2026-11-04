#ifndef SONG_SCANNER_H
#define SONG_SCANNER_H

#include <QObject>
#include <QMap>
#include <QSet>
#include <QStringList>
#include "playlist_data.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

class song_scanner : public QObject
{
    Q_OBJECT

public:
    explicit song_scanner(QObject* parent = nullptr);
    ~song_scanner() override;
    void enqueue(const QStringList& paths);
    void acknowledge(const QStringList& paths);
    void shutdown();

signals:
    void songs_scanned(const QMap<QString, scanned_song>& results);

private:
    void scan_loop();
    scanned_song scan(const QString& path);
    static int interrupt(void* context);

    std::atomic<bool> stopped_{false};
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<QString> pending_;
    QSet<QString> scheduled_;
    std::thread worker_;
};

#endif
