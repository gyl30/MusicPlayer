#ifndef DURATION_SCANNER_H
#define DURATION_SCANNER_H

#include <QObject>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

class duration_scanner : public QObject
{
    Q_OBJECT

public:
    explicit duration_scanner(QObject* parent = nullptr);
    ~duration_scanner() override;
    void enqueue(const QStringList& paths);
    void acknowledge(const QStringList& paths);
    void shutdown();

signals:
    void durations_ready(const QMap<QString, qint64>& durations);

private:
    void scan_loop();
    qint64 probe_duration(const QString& path);
    static int interrupt(void* context);

    std::atomic<bool> stopped_{false};
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<QString> pending_;
    QSet<QString> scheduled_;
    std::thread worker_;
};

#endif
