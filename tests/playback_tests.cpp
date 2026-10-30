#include <QtTest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QListWidget>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QPushButton>
#include <QSlider>
#include <QTabBar>
#include <QLabel>
#include "volumemeter.h"
#include "lyrics_widget.h"
#include <atomic>
#include "audio_decoder.h"
#include "audio_player.h"
#include "playback_controller.h"
#include "playlist_manager.h"
#include "playlist_window.h"
#include "music_management_dialog.h"
#include "duration_scanner.h"
#include "scoped_exit.h"
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

    int pending_durations()
    {
        QSqlQuery query("SELECT COUNT(DISTINCT s.song_id) FROM PlaylistSongs ps JOIN Songs s ON s.song_id = ps.song_id "
                        "WHERE s.duration_ms IS NULL");
        if (!query.next())
        {
            QTest::qFail("无法读取待探测歌曲数量", __FILE__, __LINE__);
            return -1;
        }
        return query.value(0).toInt();
    }

    QList<QVariantList> database_snapshot()
    {
        QList<QVariantList> rows;
        for (const QString& table : {QString("Songs"), QString("Playlists"), QString("PlaylistSongs"), QString("sqlite_sequence")})
        {
            rows.append(QVariantList{table});
            QSqlQuery query;
            if (!query.exec("SELECT * FROM " + table + " ORDER BY 1, 2"))
            {
                QTest::qFail("无法读取数据库快照", __FILE__, __LINE__);
                return {};
            }
            while (query.next())
            {
                QVariantList row;
                for (int i = 0; i < query.record().count(); ++i)
                {
                    row.append(query.value(i));
                }
                rows.append(row);
            }
        }
        return rows;
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

    void management_cancel()
    {
        playlist_window window;
        auto* manager = window.findChild<playlist_manager*>();
        const auto id = manager->get_all_playlists().first().id;
        QVERIFY(manager->add_songs_to_playlist(id, {tone_}));
        QTRY_COMPARE_WITH_TIMEOUT(pending_durations(), 0, 3000);
        const auto before = database_snapshot();
        QVERIFY(QMetaObject::invokeMethod(&window, "on_manage_playlists_action"));
        auto* page = window.findChild<music_management_dialog*>();
        auto* remove = page->findChild<QPushButton*>("removeButton");
        QVERIFY(remove != nullptr);
        QVERIFY(!remove->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(page, "on_source_select_all_clicked"));
        QVERIFY(remove->isEnabled());
        remove->click();
        QVERIFY(page->has_pending_changes());
        auto* cancel = page->findChild<QPushButton*>("cancelChangesButton");
        QVERIFY(cancel != nullptr);
        cancel->click();
        QVERIFY(!page->has_pending_changes());
        QCOMPARE(database_snapshot(), before);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_manage_playlists_action"));
        QVERIFY(!page->has_pending_changes());
    }

    void management_bulk_selection()
    {
        playlist_manager manager;
        manager.initialize_and_load();
        const auto id = manager.get_all_playlists().first().id;
        QStringList paths;
        for (int i = 0; i < 200; ++i)
            paths.append(temp_.filePath(QString("bulk-%1.wav").arg(i)));
        QVERIFY(manager.add_songs_to_playlist(id, paths));
        music_management_dialog page(&manager);
        page.reload();
        auto* songs = page.findChildren<QListWidget*>().at(1);
        QSignalSpy changed(songs, &QListWidget::itemChanged);
        QElapsedTimer timer;
        timer.start();
        QVERIFY(QMetaObject::invokeMethod(&page, "on_source_select_all_clicked"));
        qInfo() << "批量勾选(ms):" << timer.elapsed() << "逐项通知:" << changed.count();
        QCOMPARE(changed.count(), 0);
        for (int i = 0; i < songs->count(); ++i)
            QCOMPARE(songs->item(i)->checkState(), Qt::Checked);
        QVERIFY(page.findChild<QPushButton*>("removeButton")->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(&page, "on_source_select_all_clicked"));
        QCOMPARE(changed.count(), 0);
        for (int i = 0; i < songs->count(); ++i)
            QCOMPARE(songs->item(i)->checkState(), Qt::Unchecked);
        QVERIFY(!page.findChild<QPushButton*>("removeButton")->isEnabled());
    }

    void management_unchanged_return()
    {
        playlist_manager manager;
        manager.initialize_and_load();
        music_management_dialog page(&manager);
        page.reload();
        QSignalSpy returned(&page, &music_management_dialog::changes_discarded);
        QSignalSpy applied(&page, &music_management_dialog::changes_applied);
        // 占用事务，证明无修改返回不需要再开启写事务。
        QVERIFY(QSqlDatabase::database().transaction());
        DEFER(QSqlDatabase::database().rollback());
        QTimer::singleShot(0, &page, []
        {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) box->accept();
        });
        QVERIFY(QMetaObject::invokeMethod(&page, "on_done_button_clicked"));
        QCOMPARE(returned.count(), 1);
        QCOMPARE(applied.count(), 0);
    }

    void ui_controls()
    {
        const auto old_style = qApp->styleSheet();
        DEFER(qApp->setStyleSheet(old_style));
        QFile style(":/style/stylesheet.qss");
        QVERIFY(style.open(QFile::ReadOnly));
        qApp->setStyleSheet(QString::fromUtf8(style.readAll()));
        playlist_window window;
        window.show();
        window.resize(340, 400);
        QTest::qWait(50);
        auto* play = window.findChild<QPushButton*>("playPauseButton");
        QVERIFY(play != nullptr);
        QVERIFY(play->width() >= 40 && play->height() >= 40);
        for (auto* button : window.findChildren<QPushButton*>())
        {
            if (button->isVisible())
                QVERIFY(window.rect().contains(QRect(button->mapTo(&window, QPoint()), button->size())));
        }
        auto* volume = window.findChild<volume_meter*>();
        QVERIFY(volume != nullptr);
        const QRect volume_rect(volume->mapTo(&window, QPoint()), volume->size());
        const QRect play_rect(play->mapTo(&window, QPoint()), play->size());
        QVERIFY(volume_rect.center().y() >= play_rect.top() && volume_rect.center().y() <= play_rect.bottom());
        QVERIFY(volume_rect.left() > play_rect.right());
        QVERIFY(window.rect().contains(volume_rect));
        volume->setValue(50);
        QSignalSpy volume_changed(volume, &volume_meter::value_changed);
        QTest::keyClick(volume, Qt::Key_Right);
        QCOMPARE(volume->value(), 55);
        QCOMPARE(volume_changed.count(), 1);
        QTest::keyClick(volume, Qt::Key_End);
        QCOMPARE(volume->value(), 100);
        QTest::keyClick(volume, Qt::Key_Home);
        QCOMPARE(volume->value(), 0);
        auto* lyrics = window.findChild<lyrics_widget*>();
        QVERIFY(lyrics != nullptr);
        lyrics->set_lyrics({{0, "第一行中文歌词"}, {1000, "第二行中文歌词"}});
        window.findChild<QTabBar*>("libraryTabs")->setCurrentIndex(1);
        window.resize(340, 400);
        QTest::qWait(50);
        QVERIFY(volume->mapTo(&window, QPoint(0, volume->height())).y()
                <= lyrics->mapTo(&window, QPoint()).y());
        auto* mode = window.findChild<QPushButton*>("modeButton");
        auto* player = window.findChild<player_window*>();
        player->set_playback_mode(playback_mode::ListLoop);
        QSignalSpy changed(player, &player_window::playback_mode_changed);
        for (int i = 0; i < 4; ++i) QTest::mouseClick(mode, Qt::LeftButton);
        QCOMPARE(changed.count(), 4);
        QCOMPARE(mode->toolTip(), QString("列表循环"));
        auto* manage = window.findChild<QPushButton*>("manageButton");
        QVERIFY(manage != nullptr);
        QTest::mouseClick(manage, Qt::LeftButton);
        QVERIFY(window.findChild<music_management_dialog*>()->isVisible());
        auto* add = window.findChild<QPushButton*>("addMusicButton");
        QVERIFY(add != nullptr);
        QVERIFY(!add->isVisible());
        QVERIFY(!manage->isVisible());
        auto* cancel = window.findChild<QPushButton*>("cancelChangesButton");
        QVERIFY(cancel != nullptr);
        QTest::mouseClick(cancel, Qt::LeftButton);
        QVERIFY(add->isVisible());
        QVERIFY(manage->isVisible());
    }

    void stale_ready_seek_callbacks()
    {
        playback_controller controller;
        constexpr qint64 first_session = 1;
        QSignalSpy progress(&controller, &playback_controller::progress_updated);
        // 新控制器的第一次 play_file 固定生成会话 1。
        controller.play_file(tone_);
        QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty(), 1000);
        controller.play_file(tone_, 2000);
        QSignalSpy seek(&controller, &playback_controller::seek_finished);
        QVERIFY(QMetaObject::invokeMethod(&controller, "on_player_ready", Q_ARG(qint64, first_session)));
        QVERIFY(QMetaObject::invokeMethod(&controller, "on_player_seek_handled", Q_ARG(qint64, first_session)));
        QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty() && progress.last().first().toLongLong() >= 2000, 1500);
        QTRY_COMPARE_WITH_TIMEOUT(seek.count(), 1, 1000);
        controller.stop();
        const int count = progress.count();
        QVERIFY(QMetaObject::invokeMethod(&controller, "on_player_ready", Q_ARG(qint64, qint64{2})));
        QVERIFY(QMetaObject::invokeMethod(&controller, "on_player_seek_handled", Q_ARG(qint64, qint64{2})));
        QTest::qWait(150);
        QCOMPARE(progress.count(), count);
        QVERIFY(!controller.is_media_loaded());
    }

    void library_views()
    {
        playlist_window window;
        window.show();
        auto* tabs = window.findChild<QTabBar*>("libraryTabs");
        QVERIFY(tabs != nullptr);
        QCOMPARE(tabs->count(), 2);
        auto* player = window.findChild<player_window*>();
        auto* duration = window.findChild<QLabel*>("durationLabel");
        player->update_track_info(0);
        QCOMPARE(duration->text(), QString("--:--"));
        player->update_track_info(266000);
        QCOMPARE(duration->text(), QString("04:26"));
        auto* tree = window.findChild<QTreeWidget*>();
        QTest::qWait(50);
        QVERIFY(tree->columnWidth(0) > tree->columnWidth(1) * 2);
        auto* controller = window.findChild<playback_controller*>();
        QSignalSpy progress(controller, &playback_controller::progress_updated);
        controller->play_file(tone_);
        QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty(), 1000);
        const auto before = progress.last().first().toLongLong();
        tabs->setCurrentIndex(1);
        QVERIFY(window.findChild<lyrics_widget*>()->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(progress.last().first().toLongLong() > before, 1000);
        tabs->setCurrentIndex(0);
        QVERIFY(window.findChild<QTreeWidget*>()->isVisible());
        controller->stop();
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
        std::atomic<Qt::HANDLE> release_thread{nullptr};
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

    void storage_atomicity_data()
    {
        QTest::addColumn<QString>("operation");
        QTest::addColumn<QString>("event");
        QTest::newRow("sort_update") << "sort" << "UPDATE";
        QTest::newRow("copy_insert") << "copy" << "INSERT";
        QTest::newRow("delete_delete") << "delete" << "DELETE";
        QTest::newRow("delete_update") << "delete" << "UPDATE";
        QTest::newRow("move_insert") << "move" << "INSERT";
        QTest::newRow("move_delete") << "move" << "DELETE";
        QTest::newRow("move_update") << "move" << "UPDATE";
        QTest::newRow("new_song_insert") << "new_song" << "INSERT";
    }

    void storage_atomicity()
    {
        QFETCH(QString, operation);
        QFETCH(QString, event);
        QTemporaryDir database_dir;
        qputenv("XDG_DATA_HOME", database_dir.path().toUtf8());
        playlist_manager manager;
        manager.initialize_and_load();
        manager.create_new_playlist("目标");
        manager.create_new_playlist("未修改");
        const auto lists = manager.get_all_playlists();
        const qint64 source_id = lists[0].id;
        const qint64 target_id = lists[1].id;
        QStringList paths;
        for (int i = 0; i < 4; ++i)
        {
            const QString path = temp_.filePath(QString("%1-%2.wav").arg(QTest::currentDataTag()).arg(i));
            QVERIFY(QFile::copy(tone_, path));
            paths.append(path);
        }
        manager.add_songs_to_playlist(source_id, paths.mid(0, 3));
        manager.add_songs_to_playlist(target_id, paths.mid(3));
        QMap<qint64, Playlist> desired;
        for (const auto& list : lists)
        {
            desired.insert(list.id, manager.get_playlist_by_id(list.id));
        }
        if (operation == "sort")
        {
            std::reverse(desired[source_id].songs.begin(), desired[source_id].songs.end());
        }
        else if (operation == "copy" || operation == "move")
        {
            desired[target_id].songs.append(desired[source_id].songs[0]);
            desired[target_id].songs.append(desired[source_id].songs[2]);
            if (operation == "move")
            {
                desired[source_id].songs = {desired[source_id].songs[1]};
            }
        }
        else if (operation == "delete")
        {
            desired[source_id].songs = {desired[source_id].songs[1], desired[source_id].songs[2]};
        }
        else
        {
            desired[target_id].songs.append({temp_.filePath("new-unrecorded.wav"), "new-unrecorded.wav"});
        }
        QTRY_COMPARE_WITH_TIMEOUT(pending_durations(), 0, 3000);
        const auto before = database_snapshot();
        QSqlQuery query;
        QString condition;
        if (event == "INSERT")
        {
            condition = QString("NEW.song_id = %1").arg(3);
        }
        else if (event == "DELETE")
        {
            condition = QString("OLD.song_id = %1").arg(operation == "move" ? 3 : 1);
        }
        else
        {
            condition = QString("NEW.playlist_id = %1 AND NEW.position = 0").arg(source_id);
        }
        const QString table = operation == "new_song" ? "Songs" : "PlaylistSongs";
        if (table == "Songs")
        {
            condition = "1";
        }
        QVERIFY(query.exec(QString("CREATE TRIGGER injected_failure BEFORE %1 ON %2 WHEN %3 "
                                   "BEGIN SELECT RAISE(ABORT, 'injected failure'); END").arg(event, table, condition)));
        QSignalSpy changed(&manager, &playlist_manager::songs_changed_in_playlist);
        QVERIFY(!manager.apply_changes_from_dialog(desired));
        QCOMPARE(database_snapshot(), before);
        QCOMPARE(changed.count(), 0);
        QVERIFY(query.exec("DROP TRIGGER injected_failure"));
        QVERIFY(manager.apply_changes_from_dialog(desired));
        for (auto it = desired.constBegin(); it != desired.constEnd(); ++it)
        {
            QCOMPARE(manager.get_playlist_by_id(it.key()).songs, it->songs);
        }
    }

    void storage_minimal_writes_data()
    {
        QTest::addColumn<QString>("operation");
        QTest::newRow("sort") << "sort";
        QTest::newRow("copy") << "copy";
        QTest::newRow("delete") << "delete";
    }

    void storage_minimal_writes()
    {
        QFETCH(QString, operation);
        QTemporaryDir database_dir;
        qputenv("XDG_DATA_HOME", database_dir.path().toUtf8());
        playlist_manager manager;
        manager.initialize_and_load();
        manager.create_new_playlist("目标");
        const auto lists = manager.get_all_playlists();
        const qint64 source_id = lists[0].id;
        const qint64 target_id = lists[1].id;
        manager.add_songs_to_playlist(source_id, {tone_, temp_.filePath("missing.wav")});
        manager.add_songs_to_playlist(target_id, {temp_.filePath("other.wav")});
        QMap<qint64, Playlist> desired{{source_id, manager.get_playlist_by_id(source_id)},
                                     {target_id, manager.get_playlist_by_id(target_id)}};
        QStringList forbidden;
        if (operation == "sort")
        {
            std::reverse(desired[source_id].songs.begin(), desired[source_id].songs.end());
            forbidden = {"INSERT", "DELETE"};
        }
        else if (operation == "copy")
        {
            desired[target_id].songs.append(desired[source_id].songs);
            forbidden = {"DELETE", "UPDATE"};
        }
        else
        {
            desired[source_id].songs.removeFirst();
            forbidden = {"INSERT"};
        }
        QSqlQuery query;
        for (const auto& event : forbidden)
        {
            QVERIFY(query.exec(QString("CREATE TRIGGER forbid_%1 BEFORE %1 ON PlaylistSongs "
                                       "BEGIN SELECT RAISE(ABORT, 'unnecessary write'); END").arg(event)));
        }
        QSignalSpy changed(&manager, &playlist_manager::songs_changed_in_playlist);
        QVERIFY(manager.apply_changes_from_dialog(desired));
        for (auto it = desired.constBegin(); it != desired.constEnd(); ++it)
        {
            QCOMPARE(manager.get_playlist_by_id(it.key()).songs, it->songs);
        }
        QCOMPARE(changed.count(), 1);
        QVERIFY(query.exec("SELECT playlist_id, position FROM PlaylistSongs ORDER BY playlist_id, position"));
        qint64 previous_id = -1;
        int position = 0;
        while (query.next())
        {
            if (query.value(0).toLongLong() != previous_id)
            {
                previous_id = query.value(0).toLongLong();
                position = 0;
            }
            QCOMPARE(query.value(1).toInt(), position++);
        }
    }

    void management_failure_data()
    {
        QTest::addColumn<bool>("move");
        QTest::newRow("single_delete") << false;
        QTest::newRow("cross_playlist_move") << true;
    }

    void management_failure()
    {
        QFETCH(bool, move);
        QTemporaryDir database_dir;
        qputenv("XDG_DATA_HOME", database_dir.path().toUtf8());
        playlist_manager manager;
        manager.initialize_and_load();
        const qint64 id = manager.get_all_playlists().first().id;
        manager.add_songs_to_playlist(id, {tone_});
        QVERIFY(manager.create_new_playlist("目标"));
        const qint64 target_id = manager.get_all_playlists().last().id;
        music_management_dialog page(&manager);
        page.reload();
        const auto lists = page.findChildren<QListWidget*>();
        QCOMPARE(lists.size(), 4);
        lists[0]->setCurrentRow(0);
        lists[1]->item(0)->setCheckState(Qt::Checked);
        lists[2]->setCurrentRow(1);
        QVERIFY(QMetaObject::invokeMethod(&page, move ? "on_move_button_clicked" : "on_delete_button_clicked"));
        QVERIFY(page.has_pending_changes());
        QTRY_COMPARE_WITH_TIMEOUT(pending_durations(), 0, 3000);
        const auto before = database_snapshot();
        QSqlQuery query;
        const QString event = move ? "INSERT" : "DELETE";
        QVERIFY(query.exec(QString("CREATE TRIGGER reject_apply BEFORE %1 ON PlaylistSongs "
                                   "BEGIN SELECT RAISE(ABORT, 'injected apply failure'); END").arg(event)));
        QSignalSpy applied(&page, &music_management_dialog::changes_applied);
        QTimer::singleShot(0, &page, []
        {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            {
                box->accept();
            }
        });
        QVERIFY(QMetaObject::invokeMethod(&page, "on_done_button_clicked"));
        QCOMPARE(database_snapshot(), before);
        QVERIFY(page.has_pending_changes());
        QCOMPARE(applied.count(), 0);
        QVERIFY(query.exec("DROP TRIGGER reject_apply"));
        QVERIFY(QMetaObject::invokeMethod(&page, "on_done_button_clicked"));
        QVERIFY(!page.has_pending_changes());
        QCOMPARE(applied.count(), 1);
        QCOMPARE(manager.get_playlist_by_id(id).songs.size(), 0);
        QCOMPARE(manager.get_playlist_by_id(target_id).songs.size(), move ? 1 : 0);
    }

    void storage_write_failure_data()
    {
        QTest::addColumn<QString>("event");
        QTest::newRow("add") << "INSERT";
        QTest::newRow("remove_playlist") << "DELETE";
        QTest::newRow("rename") << "UPDATE";
    }

    void storage_write_failure()
    {
        QFETCH(QString, event);
        QTemporaryDir database_dir;
        qputenv("XDG_DATA_HOME", database_dir.path().toUtf8());
        playlist_manager manager;
        manager.initialize_and_load();
        QVERIFY(manager.create_new_playlist("目标"));
        const qint64 id = manager.get_all_playlists().last().id;
        QTRY_COMPARE_WITH_TIMEOUT(pending_durations(), 0, 3000);
        const auto before = database_snapshot();
        QSqlQuery query;
        const QString table = event == "INSERT" ? "Songs" : "Playlists";
        QVERIFY(query.exec(QString("CREATE TRIGGER reject_write BEFORE %1 ON %2 "
                                   "BEGIN SELECT RAISE(ABORT, 'injected write failure'); END").arg(event, table)));
        QSignalSpy changed(&manager, &playlist_manager::songs_changed_in_playlist);
        QSignalSpy removed(&manager, &playlist_manager::playlist_removed);
        QSignalSpy renamed(&manager, &playlist_manager::playlist_renamed);
        if (event == "INSERT")
        {
            QVERIFY(!manager.add_songs_to_playlist(id, {tone_}));
        }
        else if (event == "DELETE")
        {
            QVERIFY(!manager.delete_playlist(id));
        }
        else
        {
            QVERIFY(!manager.rename_playlist(id, "新名称"));
        }
        QCOMPARE(database_snapshot(), before);
        QCOMPARE(changed.count() + removed.count() + renamed.count(), 0);
    }

    void async_import()
    {
        const QString short_file = temp_.filePath("short-import.wav");
        encode({"-i", tone_, "-t", "0.02", short_file});
        QStringList paths;
        for (int i = 0; i < 500; ++i)
        {
            const QString path = temp_.filePath(QString("import-%1.wav").arg(i));
            QVERIFY(QFile::copy(short_file, path));
            paths.append(path);
        }
        const QString corrupt = temp_.filePath("bad-import.wav");
        QFile file(corrupt);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not audio");
        file.close();
        paths.append(corrupt);
        paths.append(temp_.filePath("missing-import.wav"));
        playlist_window window;
        auto* manager = window.findChild<playlist_manager*>();
        const qint64 id = manager->get_all_playlists().first().id;
        int probes = 0;
        connect(manager->findChild<duration_scanner*>(), &duration_scanner::durations_ready, manager,
                [&](const QMap<QString, qint64>& values) { probes += static_cast<int>(values.size()); }, Qt::QueuedConnection);
        QVERIFY(manager->add_songs_to_playlist(id, paths));
        QVERIFY(pending_durations() > 0);
        auto* tree = window.findChild<QTreeWidget*>();
        QVERIFY(manager->add_songs_to_playlist(id, paths));
        auto* first = tree->topLevelItem(0)->child(0);
        QSignalSpy removed(tree->model(), &QAbstractItemModel::rowsRemoved);
        QCOMPARE(manager->get_playlist_by_id(id).songs.size(), paths.size());
        QTimer heartbeat;
        int ticks = 0;
        connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; });
        heartbeat.start(1);
        QTRY_COMPARE_WITH_TIMEOUT(pending_durations(), 0, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(probes, paths.size(), 1000);
        QTest::qWait(50);
        QCOMPARE(probes, paths.size());
        QVERIFY(ticks > 0);
        QCOMPARE(tree->topLevelItem(0)->child(0), first);
        QCOMPARE(removed.count(), 0);
        const auto songs = manager->get_playlist_by_id(id).songs;
        for (int i = 0; i < 500; ++i)
        {
            QCOMPARE(songs[i].duration_ms, 20);
        }
        QCOMPARE(songs[500].duration_ms, 0);
        QCOMPARE(songs[501].duration_ms, 0);
    }

    void metadata_removed()
    {
        playlist_manager manager;
        manager.initialize_and_load();
        const qint64 id = manager.get_all_playlists().first().id;
        int probes = 0;
        connect(manager.findChild<duration_scanner*>(), &duration_scanner::durations_ready, &manager,
                [&](const QMap<QString, qint64>& values) { probes += static_cast<int>(values.size()); }, Qt::QueuedConnection);
        QSignalSpy updates(&manager, &playlist_manager::durations_updated);
        QVERIFY(manager.add_songs_to_playlist(id, {tone_, temp_.filePath("removed-missing.wav")}));
        QVERIFY(manager.remove_songs_from_playlist(id, {0, 1}));
        QTRY_COMPARE_WITH_TIMEOUT(probes, 2, 1000);
        QCOMPARE(updates.count(), 0);
        QSqlQuery query("SELECT COUNT(*) FROM Songs WHERE duration_ms IS NOT NULL");
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 0);
        QVERIFY(manager.add_songs_to_playlist(id, {tone_}));
        QTRY_COMPARE_WITH_TIMEOUT(pending_durations(), 0, 1000);
        QVERIFY(manager.get_playlist_by_id(id).songs.first().duration_ms > 0);
    }

    void metadata_update_failure()
    {
        playlist_manager manager;
        manager.initialize_and_load();
        const qint64 id = manager.get_all_playlists().first().id;
        QStringList paths;
        for (int i = 0; i < 4; ++i)
        {
            const QString path = temp_.filePath(QString("duration-%1.wav").arg(i));
            QVERIFY(QFile::copy(tone_, path));
            paths.append(path);
        }
        QSqlQuery query;
        QVERIFY(query.exec("CREATE TRIGGER reject_duration BEFORE UPDATE OF duration_ms ON Songs "
                           "WHEN NEW.file_path LIKE '%duration-2.wav' "
                           "BEGIN SELECT RAISE(ABORT, 'injected duration failure'); END"));
        int probes = 0;
        connect(manager.findChild<duration_scanner*>(), &duration_scanner::durations_ready, &manager,
                [&](const QMap<QString, qint64>& values) { probes += static_cast<int>(values.size()); }, Qt::QueuedConnection);
        QSignalSpy updates(&manager, &playlist_manager::durations_updated);
        QVERIFY(manager.add_songs_to_playlist(id, paths));
        QTRY_COMPARE_WITH_TIMEOUT(probes, 4, 1000);
        QCOMPARE(pending_durations(), 4);
        QCOMPARE(updates.count(), 0);
        QVERIFY(query.exec("DROP TRIGGER reject_duration"));
        QVERIFY(manager.add_songs_to_playlist(id, paths));
        QTRY_COMPARE_WITH_TIMEOUT(pending_durations(), 0, 1000);
        QCOMPARE(manager.get_playlist_by_id(id).songs.size(), 4);
    }

    void metadata_scan_shutdown()
    {
        const auto level = spdlog::get_level();
        spdlog::set_level(spdlog::level::off);
        DEFER(spdlog::set_level(level));
        QStringList paths;
        for (int i = 0; i < 5000; ++i)
        {
            paths.append(temp_.filePath(QString("cancel-%1.wav").arg(i)));
        }
        std::atomic<bool> first_batch{false};
        std::atomic<bool> release{false};
        {
            playlist_manager manager;
            manager.initialize_and_load();
            auto* scanner = manager.findChild<duration_scanner*>();
            connect(scanner, &duration_scanner::durations_ready, scanner, [&](const QMap<QString, qint64>&)
            {
                first_batch = true;
                while (!release)
                {
                    QThread::msleep(1);
                }
            }, Qt::DirectConnection);
            DEFER(release = true);
            const qint64 id = manager.get_all_playlists().first().id;
            QVERIFY(manager.add_songs_to_playlist(id, paths));
            QTRY_VERIFY_WITH_TIMEOUT(first_batch.load(), 1000);
            QVERIFY(pending_durations() > 0);
            release = true;
        }
        QCoreApplication::processEvents();
        QVERIFY(QSqlDatabase::database().isOpen());
        QSqlQuery query("SELECT COUNT(*) FROM PlaylistSongs");
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), paths.size());
        QVERIFY(pending_durations() > 0);
    }

    void close_without_tray()
    {
        if (QSystemTrayIcon::isSystemTrayAvailable())
        {
            QSKIP("该回归需要没有系统托盘服务的会话");
        }
        QApplication::setWindowIcon(QIcon(":/icons/app_icon.svg"));
        playlist_window window;
        window.show();
        QTest::qWait(50);
        QVERIFY(window.close());
        QVERIFY(!window.isVisible());
    }

    void metadata_path_reuse()
    {
        playlist_manager manager;
        manager.initialize_and_load();
        const qint64 id = manager.get_all_playlists().first().id;
        int probes = 0;
        connect(manager.findChild<duration_scanner*>(), &duration_scanner::durations_ready, &manager,
                [&](const QMap<QString, qint64>& values) { probes += static_cast<int>(values.size()); }, Qt::QueuedConnection);
        QVERIFY(manager.add_songs_to_playlist(id, {tone_}));
        QVERIFY(manager.remove_songs_from_playlist(id, {0}));
        QVERIFY(manager.add_songs_to_playlist(id, {tone_}));
        QTRY_COMPARE_WITH_TIMEOUT(pending_durations(), 0, 1000);
        QTRY_COMPARE_WITH_TIMEOUT(probes, 1, 1000);
        QCOMPARE(manager.get_playlist_by_id(id).songs.size(), 1);
        QVERIFY(manager.get_playlist_by_id(id).songs.first().duration_ms > 0);
    }

    void playback_failure_limit_data()
    {
        QTest::addColumn<bool>("successful_track");
        QTest::newRow("all_missing") << false;
        QTest::newRow("reset_after_audio") << true;
    }

    void playback_failure_limit()
    {
        QFETCH(bool, successful_track);
        constexpr int kFailureLimit = 5;
        QTemporaryDir database_dir;
        qputenv("XDG_DATA_HOME", database_dir.path().toUtf8());
        playlist_window window;
        auto* manager = window.findChild<playlist_manager*>();
        auto* controller = window.findChild<playback_controller*>();
        const qint64 id = manager->get_all_playlists().first().id;
        QStringList paths;
        for (int i = 0; i < 4; ++i)
        {
            paths.append(temp_.filePath(QString("missing-before-%1.wav").arg(i)));
        }
        if (successful_track)
        {
            paths.append(tone_);
        }
        for (int i = 0; i < kFailureLimit; ++i)
        {
            paths.append(temp_.filePath(QString("missing-after-%1.wav").arg(i)));
        }
        QVERIFY(manager->add_songs_to_playlist(id, paths));
        QVERIFY(QMetaObject::invokeMethod(&window, "on_playback_mode_changed", Q_ARG(playback_mode, playback_mode::ListLoop)));
        auto* tree = window.findChild<QTreeWidget*>();
        QSignalSpy errors(controller, &playback_controller::playback_error);
        QSignalSpy progress(controller, &playback_controller::progress_updated);
        bool stopped_dialog = false;
        QTimer close_dialog;
        connect(&close_dialog, &QTimer::timeout, &window, [&]
        {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            {
                stopped_dialog = box->text().contains(QString("连续 %1 首").arg(kFailureLimit));
                box->accept();
            }
        });
        close_dialog.start(10);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_tree_item_double_clicked",
                                          Q_ARG(QTreeWidgetItem*, tree->topLevelItem(0)->child(0)), Q_ARG(int, 0)));
        if (successful_track)
        {
            QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty() && progress.last().first().toLongLong() > 0, 1800);
            QCOMPARE(errors.count(), 4);
            QVERIFY(!stopped_dialog);
            QVERIFY(QMetaObject::invokeMethod(&window, "on_next_requested"));
        }
        QTRY_VERIFY_WITH_TIMEOUT(stopped_dialog, 1800);
        const int expected_errors = successful_track ? 4 + kFailureLimit : kFailureLimit;
        QCOMPARE(errors.count(), expected_errors);
        QVERIFY(!controller->is_media_loaded());
        QTest::qWait(350);
        QCOMPARE(errors.count(), expected_errors);
    }

    void shuffle_manual_selection()
    {
        playlist_window window;
        auto* manager = window.findChild<playlist_manager*>();
        const qint64 id = manager->get_all_playlists().first().id;
        QStringList paths;
        for (int i = 0; i < 6; ++i)
        {
            const QString path = temp_.filePath(QString("manual-%1.wav").arg(i));
            QVERIFY(QFile::copy(tone_, path));
            paths.append(path);
        }
        QVERIFY(manager->add_songs_to_playlist(id, paths));
        auto* list = window.findChild<QTreeWidget*>()->topLevelItem(0);
        auto* tree = window.findChild<QTreeWidget*>();
        QVERIFY(QMetaObject::invokeMethod(&window, "on_playback_mode_changed", Q_ARG(playback_mode, playback_mode::Shuffle)));
        for (int selected : {0, 3, 1, 5})
        {
            auto* chosen = list->child(selected);
            QVERIFY(QMetaObject::invokeMethod(&window, "on_tree_item_double_clicked", Q_ARG(QTreeWidgetItem*, chosen), Q_ARG(int, 0)));
            QVERIFY(QMetaObject::invokeMethod(&window, "on_next_requested"));
            QVERIFY(tree->currentItem() != chosen);
            QVERIFY(QMetaObject::invokeMethod(&window, "on_previous_requested"));
            QCOMPARE(tree->currentItem(), chosen);
        }
        QVERIFY(QMetaObject::invokeMethod(&window, "on_stop_requested"));
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
