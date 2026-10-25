#include <QtTest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <atomic>
#include "audio_decoder.h"
#include "audio_player.h"
#include "playback_controller.h"
#include "playlist_manager.h"
#include "playlist_window.h"
#include "log.h"

class playback_tests : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir temp_;
    QString tone_;

    void encode(const QStringList& arguments)
    {
        QProcess process;
        process.start(FFMPEG_EXECUTABLE, QStringList{"-v", "error", "-y"} + arguments);
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0);
    }

private slots:
    void initTestCase()
    {
        spdlog::set_level(spdlog::level::warn);
        QVERIFY(temp_.isValid());
        qputenv("XDG_DATA_HOME", temp_.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", temp_.path().toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp_.path());
        tone_ = temp_.filePath("tone.wav");
        encode({"-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=12", tone_});
    }

    void unknown_duration()
    {
        const QString file = temp_.filePath("unknown.mka");
        encode({"-i", tone_, "-c:a", "flac", "-live", "1", file});
        AVFormatContext* context = nullptr;
        QCOMPARE(avformat_open_input(&context, file.toUtf8().constData(), nullptr, nullptr), 0);
        QVERIFY(avformat_find_stream_info(context, nullptr) >= 0);
        QCOMPARE(context->duration, AV_NOPTS_VALUE);
        avformat_close_input(&context);

        playback_controller controller;
        QSignalSpy info(&controller, &playback_controller::track_info_ready);
        QSignalSpy progress(&controller, &playback_controller::progress_updated);
        controller.play_file(file);
        QTRY_COMPARE_WITH_TIMEOUT(info.count(), 1, 1000);
        QCOMPARE(info.first().first().toLongLong(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty() && progress.last().first().toLongLong() > 0, 1000);
        controller.stop();
    }

    void latest_seek()
    {
        playback_controller controller;
        QSignalSpy info(&controller, &playback_controller::track_info_ready);
        QSignalSpy done(&controller, &playback_controller::seek_completed);
        controller.play_file(tone_);
        QTRY_COMPARE_WITH_TIMEOUT(info.count(), 1, 1000);
        QTest::qWait(100);
        controller.seek(1000);
        controller.seek(3000);
        controller.seek(6000);
        QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty() && done.last().first().toLongLong() == 6000, 1500);
        controller.seek(2000);
        QTRY_COMPARE_WITH_TIMEOUT(done.last().first().toLongLong(), 2000, 1000);
        controller.stop();
    }

    void paused_seek()
    {
        playback_controller controller;
        QSignalSpy progress(&controller, &playback_controller::progress_updated);
        QSignalSpy done(&controller, &playback_controller::seek_finished);
        controller.play_file(tone_);
        QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty(), 1000);
        controller.pause_resume();
        QTest::qWait(60);
        controller.seek(3000);
        QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty(), 1000);
        QTest::qWait(150);
        const qint64 paused_position = progress.last().first().toLongLong();
        QTest::qWait(200);
        QCOMPARE(progress.last().first().toLongLong(), paused_position);
        controller.pause_resume();
        QTRY_VERIFY_WITH_TIMEOUT(progress.last().first().toLongLong() > paused_position, 1000);
        controller.stop();
    }

    void exit_while_decoding()
    {
        auto controller = std::make_unique<playback_controller>();
        QSignalSpy progress(controller.get(), &playback_controller::progress_updated);
        controller->play_file(tone_);
        QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty(), 1000);
        // 让工作线程阻塞在下一包的同步交付上，再从 UI 线程销毁。
        QThread::msleep(50);
        controller.reset();
    }

    void rapid_switch()
    {
        playback_controller controller;
        for (int i = 0; i < 15; ++i)
        {
            controller.play_file(tone_, i % 2 == 0 ? 0 : 4000);
            QTest::qWait(5);
        }
        QSignalSpy progress(&controller, &playback_controller::progress_updated);
        QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty(), 1500);
        for (int i = 0; i < 10; ++i)
        {
            controller.pause_resume();
            QTest::qWait(5);
            controller.pause_resume();
        }
        controller.stop();
    }

    void formats()
    {
        for (const QString& extension : {QString("wav"), QString("flac"), QString("mp3"), QString("aac")})
        {
            const QString file = temp_.filePath("short." + extension);
            encode({"-i", tone_, "-t", "0.3", file});
            playback_controller controller;
            QSignalSpy finished(&controller, &playback_controller::playback_finished);
            QSignalSpy errors(&controller, &playback_controller::playback_error);
            controller.play_file(file);
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1500);
            QCOMPARE(errors.count(), 0);
            controller.stop();
        }
    }

    void invalid_files()
    {
        const QString corrupt = temp_.filePath("corrupt.mp3");
        QFile file(corrupt);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("invalid audio");
        file.close();
        playback_controller controller;
        for (const QString& path : {temp_.filePath("missing.wav"), corrupt})
        {
            QSignalSpy errors(&controller, &playback_controller::playback_error);
            controller.play_file(path);
            QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 1000);
            QVERIFY(!controller.is_media_loaded());
        }
        QSignalSpy progress(&controller, &playback_controller::progress_updated);
        controller.play_file(tone_);
        QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty(), 1000);
        controller.stop();
    }

    void startup_seek()
    {
        playback_controller controller;
        QSignalSpy done(&controller, &playback_controller::seek_completed);
        QSignalSpy progress(&controller, &playback_controller::progress_updated);
        QSignalSpy finished(&controller, &playback_controller::playback_finished);
        controller.play_file(tone_, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 1000);
        QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty() && progress.last().first().toLongLong() >= 3000, 1000);
        controller.seek(12000);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1000);
        QTest::qWait(150);
        QCOMPARE(finished.count(), 1);
        controller.stop();
    }

    void callback_ownership()
    {
        audio_player player;
        QAudioFormat format;
        format.setSampleRate(48000);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);
        const Qt::HANDLE owner = QThread::currentThreadId();
        std::atomic<Qt::HANDLE> signal_thread{nullptr};
        std::atomic<Qt::HANDLE> release_thread{nullptr};
        connect(&player, &audio_player::packet_played, &player,
                [&](const std::shared_ptr<audio_packet>&) { signal_thread = QThread::currentThreadId(); }, Qt::DirectConnection);
        player.start_playback(1, format);
        auto packet = std::shared_ptr<audio_packet>(new audio_packet, [&](audio_packet* value)
        {
            release_thread = QThread::currentThreadId();
            delete value;
        });
        packet->data.resize(960);
        packet->ms = 0;
        player.enqueue_packet(1, packet);
        packet.reset();
        QTRY_VERIFY_WITH_TIMEOUT(release_thread.load() != nullptr, 1000);
        QCOMPARE(signal_thread.load(), owner);
        QCOMPARE(release_thread.load(), owner);
        player.stop_playback();
    }

    void stale_errors()
    {
        playback_controller controller;
        controller.play_file(temp_.filePath("missing.wav"));
        // 让错误排入 UI 事件队列，然后切换到有效文件。
        QThread::msleep(50);
        controller.play_file(tone_);
        QSignalSpy errors(&controller, &playback_controller::playback_error);
        QSignalSpy progress(&controller, &playback_controller::progress_updated);
        QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty(), 1000);
        QCOMPARE(errors.count(), 0);
        controller.stop();
    }

    void short_packets()
    {
        const QString file = temp_.filePath("short-packets.opus");
        encode({"-i", tone_, "-c:a", "libopus", "-frame_duration", "2.5", "-application", "lowdelay", file});
        playback_controller controller;
        QSignalSpy progress(&controller, &playback_controller::progress_updated);
        controller.play_file(file);
        QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty(), 1000);
        controller.pause_resume();
        QTest::qWait(300);
        const qint64 paused_position = progress.last().first().toLongLong();
        controller.pause_resume();
        QTRY_VERIFY_WITH_TIMEOUT(progress.last().first().toLongLong() > paused_position, 1000);
        controller.stop();
    }

    void real_files()
    {
        const QStringList files = QString::fromUtf8(qgetenv("MUSICPLAYER_TEST_FILES")).split('\n', Qt::SkipEmptyParts);
        if (files.isEmpty())
        {
            QSKIP("未指定 MUSICPLAYER_TEST_FILES，真实文件验证需显式运行");
        }
        playback_controller controller;
        for (const QString& file : files)
        {
            QSignalSpy info(&controller, &playback_controller::track_info_ready);
            QSignalSpy progress(&controller, &playback_controller::progress_updated);
            QSignalSpy seek(&controller, &playback_controller::seek_finished);
            QSignalSpy errors(&controller, &playback_controller::playback_error);
            controller.play_file(file);
            QTRY_COMPARE_WITH_TIMEOUT(info.count(), 1, 3000);
            QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty(), 1000);
            const qint64 duration = info.first().first().toLongLong();
            controller.pause_resume();
            controller.seek(duration / 2);
            QTRY_VERIFY_WITH_TIMEOUT(!seek.isEmpty(), 1000);
            QVERIFY(seek.last().first().toBool());
            controller.pause_resume();
            controller.seek(qMax<qint64>(0, duration - 300));
            QTRY_COMPARE_WITH_TIMEOUT(seek.count(), 2, 1000);
            QCOMPARE(errors.count(), 0);
            qInfo().noquote() << "真实文件完成:" << file << "时长(ms):" << duration;
            controller.stop();
        }
    }

    void real_import()
    {
        const QStringList files = QString::fromUtf8(qgetenv("MUSICPLAYER_IMPORT_FILES")).split('\n', Qt::SkipEmptyParts);
        if (files.isEmpty())
        {
            QSKIP("未指定 MUSICPLAYER_IMPORT_FILES，导入测量需显式运行");
        }
        playlist_manager manager;
        manager.initialize_and_load();
        const qint64 id = manager.get_all_playlists().first().id;
        int ticks = 0;
        QTimer heartbeat;
        heartbeat.setInterval(10);
        connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; });
        heartbeat.start();
        QElapsedTimer timer;
        timer.start();
        manager.add_songs_to_playlist(id, files);
        qInfo() << "首次导入文件数:" << files.size() << "耗时(ms):" << timer.elapsed() << "UI 心跳:" << ticks;
        QCOMPARE(manager.get_playlist_by_id(id).songs.size(), files.size());
        timer.restart();
        manager.add_songs_to_playlist(id, files);
        qInfo() << "重复导入耗时(ms):" << timer.elapsed();
        QCOMPARE(manager.get_playlist_by_id(id).songs.size(), files.size());
    }

    void mode_boundaries()
    {
        playlist_window window;
        auto* manager = window.findChild<playlist_manager*>();
        const qint64 id = manager->get_all_playlists().first().id;
        const QString second_path = temp_.filePath("second.wav");
        QVERIFY(QFile::copy(tone_, second_path));
        manager->add_songs_to_playlist(id, {tone_, second_path});
        auto* tree = window.findChild<QTreeWidget*>();
        auto* list = tree->topLevelItem(0);
        auto* first = list->child(0);
        auto* second = list->child(1);
        auto* controller = window.findChild<playback_controller*>();
        QVERIFY(QMetaObject::invokeMethod(&window, "on_tree_item_double_clicked", Q_ARG(QTreeWidgetItem*, first), Q_ARG(int, 0)));
        list->setExpanded(false);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_next_requested"));
        QCOMPARE(tree->currentItem(), second);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_next_requested"));
        QCOMPARE(tree->currentItem(), first);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_previous_requested"));
        QCOMPARE(tree->currentItem(), second);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_playback_mode_changed", Q_ARG(playback_mode, playback_mode::SingleLoop)));
        QSignalSpy started(controller, &playback_controller::playback_started);
        QVERIFY(QMetaObject::invokeMethod(&window, "handle_playback_finished"));
        QCOMPARE(started.count(), 1);
        QCOMPARE(started.first().first().toString(), second_path);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_playback_mode_changed", Q_ARG(playback_mode, playback_mode::Sequential)));
        QVERIFY(QMetaObject::invokeMethod(&window, "on_next_requested"));
        QVERIFY(!controller->is_media_loaded());
        QVERIFY(QMetaObject::invokeMethod(&window, "on_stop_requested"));
    }

    void shuffle_history()
    {
        playlist_window window;
        auto* manager = window.findChild<playlist_manager*>();
        const auto playlist = manager->get_all_playlists().first();
        QStringList paths;
        for (int i = 0; i < 8; ++i)
        {
            const QString path = temp_.filePath(QString("song%1.wav").arg(i));
            QVERIFY(QFile::copy(tone_, path));
            paths.append(path);
        }
        manager->add_songs_to_playlist(playlist.id, paths);
        auto* tree = window.findChild<QTreeWidget*>();
        auto* first = tree->topLevelItem(0)->child(0);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_playback_mode_changed", Q_ARG(playback_mode, playback_mode::Shuffle)));
        QVERIFY(QMetaObject::invokeMethod(&window, "on_tree_item_double_clicked", Q_ARG(QTreeWidgetItem*, first), Q_ARG(int, 0)));
        // 每轮“下一首、上一首”必须返回同一首；持续重洗会破坏历史。
        for (int i = 0; i < 6; ++i)
        {
            QVERIFY(QMetaObject::invokeMethod(&window, "on_next_requested"));
            QVERIFY(QMetaObject::invokeMethod(&window, "on_previous_requested"));
            QCOMPARE(tree->currentItem(), first);
        }
        QVERIFY(QMetaObject::invokeMethod(&window, "on_stop_requested"));
    }

    void remove_playlist()
    {
        playlist_window window;
        auto* manager = window.findChild<playlist_manager*>();
        const auto playlist = manager->get_all_playlists().first();
        manager->create_new_playlist("保留列表");
        manager->add_songs_to_playlist(playlist.id, {tone_});
        auto* tree = window.findChild<QTreeWidget*>();
        auto* song = tree->topLevelItem(0)->child(0);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_tree_item_double_clicked", Q_ARG(QTreeWidgetItem*, song), Q_ARG(int, 0)));
        auto* controller = window.findChild<playback_controller*>();
        QTRY_VERIFY_WITH_TIMEOUT(controller->is_media_loaded(), 1000);
        manager->delete_playlist(playlist.id);
        QVERIFY(!controller->is_media_loaded());
        QVERIFY(QMetaObject::invokeMethod(&window, "on_stop_requested"));
    }

    void remove_song()
    {
        playlist_window window;
        auto* manager = window.findChild<playlist_manager*>();
        const auto playlist = manager->get_all_playlists().first();
        manager->add_songs_to_playlist(playlist.id, {tone_});
        auto* tree = window.findChild<QTreeWidget*>();
        auto* song = tree->topLevelItem(0)->child(0);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_tree_item_double_clicked", Q_ARG(QTreeWidgetItem*, song), Q_ARG(int, 0)));
        auto* controller = window.findChild<playback_controller*>();
        QTRY_VERIFY_WITH_TIMEOUT(controller->is_media_loaded(), 1000);
        manager->remove_songs_from_playlist(playlist.id, {0});
        QVERIFY(!controller->is_media_loaded());
        QVERIFY(QMetaObject::invokeMethod(&window, "on_stop_requested"));
    }

    void refresh_nodes()
    {
        class tracked_item : public QTreeWidgetItem
        {
        public:
            tracked_item(QTreeWidgetItem* parent, int& count) : QTreeWidgetItem(parent), count_(count) {}
            ~tracked_item() override { ++count_; }
        private:
            int& count_;
        };
        int destroyed = 0;
        playlist_window window;
        auto* manager = window.findChild<playlist_manager*>();
        const auto playlist = manager->get_all_playlists().first();
        auto* tree = window.findChild<QTreeWidget*>();
        new tracked_item(tree->topLevelItem(0), destroyed);
        manager->add_songs_to_playlist(playlist.id, {tone_});
        QCOMPARE(destroyed, 1);
    }
};

QTEST_MAIN(playback_tests)
#include "playback_tests.moc"
