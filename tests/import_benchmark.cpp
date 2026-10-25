#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QTimer>
#include <QThread>
#include <QElapsedTimer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTreeWidget>
#include <sqlite3.h>
#include <sys/resource.h>
#include <dlfcn.h>
#include <atomic>
#include "playlist_window.h"
#include "playlist_manager.h"
#include "log.h"
extern "C" {
#include <libavformat/avformat.h>
int __real_avformat_open_input(AVFormatContext**, const char*, const AVInputFormat*, AVDictionary**);
int __real_avformat_find_stream_info(AVFormatContext*, AVDictionary**);
}
static std::atomic<qint64> probe_ns{0};
static std::atomic<qint64> sql_ns{0};
std::atomic<qint64> benchmark_ui_ns{0};
std::atomic<qint64> benchmark_metadata_ui_ns{0};
extern "C" int __wrap_avformat_open_input(AVFormatContext** context, const char* file, const AVInputFormat* format, AVDictionary** options)
{
    QElapsedTimer timer;
    timer.start();
    const int result = __real_avformat_open_input(context, file, format, options);
    probe_ns += timer.nsecsElapsed();
    return result;
}
extern "C" int __wrap_avformat_find_stream_info(AVFormatContext* context, AVDictionary** options)
{
    QElapsedTimer timer;
    timer.start();
    const int result = __real_avformat_find_stream_info(context, options);
    probe_ns += timer.nsecsElapsed();
    return result;
}
extern "C" int sqlite3_step(sqlite3_stmt* statement)
{
    static auto original = reinterpret_cast<int (*)(sqlite3_stmt*)>(dlsym(dlopen("libsqlite3.so.0", RTLD_NOW | RTLD_LOCAL), "sqlite3_step"));
    QElapsedTimer timer;
    timer.start();
    const int result = original(statement);
    sql_ns += timer.nsecsElapsed();
    return result;
}
int main(int argc, char** argv)
{
    if (argc != 2) { return 2; }
    QTemporaryDir temp;
    qputenv("XDG_DATA_HOME", temp.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", temp.path().toUtf8());
    QApplication app(argc, argv);
    spdlog::set_level(spdlog::level::off);
    QFile file(QString::fromLocal8Bit(argv[1]));
    if (!file.open(QIODevice::ReadOnly)) { return 2; }
    const QStringList paths = QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
    playlist_window window;
    auto* manager = window.findChild<playlist_manager*>();
    const qint64 id = manager->get_all_playlists().first().id;
    for (int repeat = 0; repeat < 2; ++repeat)
    {
        probe_ns = 0;
        sql_ns = 0;
        benchmark_ui_ns = 0;
        benchmark_metadata_ui_ns = 0;
        qint64 maximum_gap_ns = 0;
        int ticks = 0;
        QElapsedTimer heartbeat_clock;
        heartbeat_clock.start();
        QTimer heartbeat;
        QObject::connect(&heartbeat, &QTimer::timeout, [&]
        {
            maximum_gap_ns = qMax(maximum_gap_ns, heartbeat_clock.nsecsElapsed());
            heartbeat_clock.restart();
            ++ticks;
        });
        heartbeat.start(10);
        QElapsedTimer total;
        total.start();
        manager->add_songs_to_playlist(id, paths);
        const double returned_ms = static_cast<double>(total.nsecsElapsed()) / 1000000.0;
        bool complete = false;
        while (total.elapsed() < 90000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QSqlQuery pending("SELECT COUNT(DISTINCT s.song_id) FROM Songs s JOIN PlaylistSongs ps "
                              "ON ps.song_id = s.song_id WHERE s.duration_ms IS NULL");
            if (pending.next() && pending.value(0).toInt() == 0)
            {
                complete = true;
                break;
            }
            QThread::msleep(5);
        }
        const double completed_ms = static_cast<double>(total.nsecsElapsed()) / 1000000.0;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(12);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        heartbeat.stop();
        QSqlQuery count("SELECT COUNT(*) FROM PlaylistSongs");
        count.next();
        bool consistent = true;
        QSqlQuery links("SELECT s.file_path, ps.position FROM PlaylistSongs ps "
                        "JOIN Songs s ON s.song_id = ps.song_id ORDER BY ps.position");
        int position = 0;
        while (links.next())
        {
            if (position >= paths.size() || links.value(0).toString() != paths[position] || links.value(1).toInt() != position)
            {
                consistent = false;
            }
            ++position;
        }
        QSqlQuery foreign_keys("PRAGMA foreign_key_check");
        consistent = consistent && position == paths.size() && !foreign_keys.next();
        rusage usage{};
        getrusage(RUSAGE_SELF, &usage);
        QJsonObject result{{"files", paths.size()}, {"repeat", repeat}, {"return_ms", returned_ms},
                           {"complete_ms", completed_ms}, {"probe_ms", static_cast<double>(probe_ns.load()) / 1000000.0},
                           {"sqlite_step_ms", static_cast<double>(sql_ns.load()) / 1000000.0},
                           {"ui_refresh_ms", static_cast<double>(benchmark_ui_ns.load()) / 1000000.0},
                           {"metadata_ui_ms", static_cast<double>(benchmark_metadata_ui_ns.load()) / 1000000.0},
                           {"max_ui_gap_ms", static_cast<double>(maximum_gap_ns) / 1000000.0},
                           {"ui_ticks", ticks}, {"peak_rss_kib", static_cast<qint64>(usage.ru_maxrss)},
                           {"rows", count.value(0).toInt()}, {"complete", complete}, {"consistent", consistent}};
        puts(QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
        if (!complete || !consistent || count.value(0).toInt() != paths.size()) { return 4; }
    }
}
