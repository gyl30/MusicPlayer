#include <QtTest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QDir>
#include <QFileInfo>
#include <QListWidget>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QPushButton>
#include <QSlider>
#include <QScrollBar>
#include <QFileDialog>
#include <QLineEdit>
#include <QLabel>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QIcon>
#include <QMenu>
#include "volumemeter.h"
#include "lyrics_widget.h"
#include "lyrics_parser.h"
#include <atomic>
#include "audio_decoder.h"
#include "audio_player.h"
#include "playback_controller.h"
#include "playlist_manager.h"
#include "database_manager.h"
#include "playlist_window.h"
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
    void init()
    {
        const QString test_scope = QStringLiteral("MusicPlayerTests-%1-%2-%3")
            .arg(QFileInfo(temp_.path()).fileName(), QString::fromLatin1(QTest::currentTestFunction()),
                 QString::fromLatin1(QTest::currentDataTag()));
        QCoreApplication::setOrganizationName(QStringLiteral("MusicPlayerTests"));
        QCoreApplication::setApplicationName(test_scope);
    }

    void cleanup()
    {
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    }

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

    void database_connection_lifecycle()
    {
        QVERIFY(!QSqlDatabase::contains());
        {
            database_manager manager;
            QVERIFY(manager.initialize());
            QVERIFY(QSqlDatabase::contains());
        }
        QVERIFY(!QSqlDatabase::contains());
    }

    void lyrics_parse_tags()
    {
        const auto lines = lyrics_parser::parse("[ti:标题]\n[1:02.50]一位分钟\n[100:00.00]三位分钟\n[00:03]无小数");
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines[0].timestamp_ms, qint64{3000});
        QCOMPARE(lines[1].timestamp_ms, qint64{62500});
        QCOMPARE(lines[1].text, QString("一位分钟"));
        QCOMPARE(lines[2].timestamp_ms, qint64{6000000});

        // offset 为正表示歌词提前显示。
        const auto earlier = lyrics_parser::parse("[offset:+500]\n[00:01.00]提前\n[00:00.20]不早于零");
        QCOMPARE(earlier.size(), 2);
        QCOMPARE(earlier[0].timestamp_ms, qint64{0});
        QCOMPARE(earlier[1].timestamp_ms, qint64{500});
        const auto later = lyrics_parser::parse("[00:01.00]推后\n[offset:-250]");
        QCOMPARE(later.first().timestamp_ms, qint64{1250});
    }

    static void accept_question_boxes(QTimer& timer, QObject* context)
    {
        connect(&timer, &QTimer::timeout, context, []
        {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            {
                if (auto* yes = box->button(QMessageBox::Yes)) yes->click();
                else box->accept();
            }
        });
        timer.start(10);
    }

    void cleanup_invalid_files()
    {
        playlist_window window;
        auto* manager = window.findChild<playlist_manager*>();
        QVERIFY(manager->create_new_playlist("清理测试"));
        const auto lists = manager->get_all_playlists();
        const qint64 first = lists.first().id;
        const qint64 second = lists.last().id;
        QVERIFY(manager->add_songs_to_playlist(first, {tone_, temp_.filePath("cleanup-missing-a.wav")}));
        QVERIFY(manager->add_songs_to_playlist(second, {temp_.filePath("cleanup-missing-b.wav")}));

        QTimer confirm;
        accept_question_boxes(confirm, &window);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_cleanup_invalid_action"));
        confirm.stop();

        const Playlist kept = manager->get_playlist_by_id(first);
        QCOMPARE(kept.songs.size(), 1);
        QCOMPARE(kept.songs.first().file_path, tone_);
        QCOMPARE(manager->get_playlist_by_id(second).songs.size(), 0);
        QVERIFY(QFileInfo::exists(tone_));
        auto* tree = window.findChild<QTreeWidget*>();
        QCOMPARE(tree->topLevelItem(0)->childCount(), 1);
        QCOMPARE(tree->topLevelItem(1)->childCount(), 0);
    }

    void transfer_songs()
    {
        playlist_window window;
        auto* manager = window.findChild<playlist_manager*>();
        QVERIFY(manager->create_new_playlist("目标"));
        const auto lists = manager->get_all_playlists();
        const qint64 source_id = lists.first().id;
        const qint64 target_id = lists.last().id;
        QStringList paths;
        for (int i = 0; i < 3; ++i)
        {
            paths.append(temp_.filePath(QString("transfer-%1.wav").arg(i)));
        }
        QVERIFY(manager->add_songs_to_playlist(source_id, paths));
        QVERIFY(manager->add_songs_to_playlist(target_id, {paths[2]}));
        auto* tree = window.findChild<QTreeWidget*>();
        auto* source = tree->topLevelItem(0);

        window.show();
        source->setExpanded(true);
        QStringList menu_entries;
        QTimer inspect_menu;
        connect(&inspect_menu, &QTimer::timeout, &window, [&]
        {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (menu == nullptr) return;
            for (auto* action : menu->actions())
            {
                menu_entries.append(action->text());
                if (action->menu() != nullptr)
                {
                    for (auto* target : action->menu()->actions()) menu_entries.append(action->text() + "/" + target->text());
                }
            }
            menu->close();
        });
        inspect_menu.start(10);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_song_tree_context_menu_requested",
                                          Q_ARG(QPoint, tree->visualItemRect(source->child(0)).center())));
        inspect_menu.stop();
        QVERIFY(menu_entries.contains("添加到歌单/目标"));
        QVERIFY(menu_entries.contains("移动到歌单/目标"));
        QVERIFY(menu_entries.contains("从播放列表移除"));
        tree->clearSelection();

        source->child(0)->setSelected(true);
        source->child(2)->setSelected(true);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_copy_songs_action", Q_ARG(qint64, target_id)));
        // 已在目标歌单中的歌曲不重复添加，复制保留源歌单。
        QStringList target_paths;
        for (const auto& song : manager->get_playlist_by_id(target_id).songs) target_paths.append(song.file_path);
        QCOMPARE(target_paths, QStringList({paths[2], paths[0]}));
        QCOMPARE(manager->get_playlist_by_id(source_id).songs.size(), 3);

        tree->clearSelection();
        source->child(1)->setSelected(true);
        source->child(2)->setSelected(true);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_move_songs_action", Q_ARG(qint64, target_id)));
        target_paths.clear();
        for (const auto& song : manager->get_playlist_by_id(target_id).songs) target_paths.append(song.file_path);
        QCOMPARE(target_paths, QStringList({paths[2], paths[0], paths[1]}));
        const auto remaining = manager->get_playlist_by_id(source_id).songs;
        QCOMPARE(remaining.size(), 1);
        QCOMPARE(remaining.first().file_path, paths[0]);
        QCOMPARE(tree->topLevelItem(0)->childCount(), 1);
        QCOMPARE(tree->topLevelItem(1)->childCount(), 3);
    }

    void drag_songs()
    {
        playlist_window window;
        window.resize(540, 740);
        window.show();
        auto* manager = window.findChild<playlist_manager*>();
        QVERIFY(manager->create_new_playlist("目标"));
        const auto lists = manager->get_all_playlists();
        const qint64 source_id = lists.first().id;
        const qint64 target_id = lists.last().id;
        QStringList paths;
        for (int i = 0; i < 4; ++i)
        {
            paths.append(temp_.filePath(QString("drag-%1.wav").arg(i)));
        }
        QVERIFY(manager->add_songs_to_playlist(source_id, paths));
        auto* tree = window.findChild<QTreeWidget*>();
        auto* source = tree->topLevelItem(0);
        source->setExpanded(true);
        QTest::qWait(50);
        const auto order = [&](qint64 id)
        {
            QStringList result;
            for (const auto& song : manager->get_playlist_by_id(id).songs) result.append(song.file_path);
            return result;
        };
        const auto drop_at = [&](const QPoint& position)
        {
            QVERIFY(QMetaObject::invokeMethod(&window, "handle_song_drop", Q_ARG(QPoint, position)));
        };

        source->child(0)->setSelected(true);
        const QRect third = tree->visualItemRect(source->child(2));
        drop_at(QPoint(third.center().x(), third.bottom() - 2));
        QTRY_COMPARE(order(source_id), QStringList({paths[1], paths[2], paths[0], paths[3]}));
        QCOMPARE(source->childCount(), 4);
        QVERIFY(source->child(2)->isSelected());

        tree->clearSelection();
        source->child(3)->setSelected(true);
        const QRect first = tree->visualItemRect(source->child(0));
        drop_at(QPoint(first.center().x(), first.top() + 2));
        QTRY_COMPARE(order(source_id), QStringList({paths[3], paths[1], paths[2], paths[0]}));

        tree->clearSelection();
        source->child(0)->setSelected(true);
        drop_at(tree->visualItemRect(tree->topLevelItem(1)).center());
        QTRY_COMPARE(order(target_id), QStringList({paths[3]}));
        QCOMPARE(order(source_id), QStringList({paths[1], paths[2], paths[0]}));
    }

    void transfer_failure()
    {
        playlist_window window;
        auto* manager = window.findChild<playlist_manager*>();
        QVERIFY(manager->create_new_playlist("目标"));
        const auto lists = manager->get_all_playlists();
        QVERIFY(manager->add_songs_to_playlist(lists.first().id, {tone_}));
        QTRY_COMPARE_WITH_TIMEOUT(pending_durations(), 0, 3000);
        window.findChild<QTreeWidget*>()->topLevelItem(0)->child(0)->setSelected(true);
        const auto before = database_snapshot();
        QSqlQuery query;
        QVERIFY(query.exec("CREATE TRIGGER reject_move BEFORE INSERT ON PlaylistSongs "
                           "BEGIN SELECT RAISE(ABORT, 'injected move failure'); END"));
        QTimer close_warning;
        accept_question_boxes(close_warning, &window);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_move_songs_action", Q_ARG(qint64, lists.last().id)));
        close_warning.stop();
        QCOMPARE(database_snapshot(), before);
        QCOMPARE(window.findChild<QTreeWidget*>()->topLevelItem(0)->childCount(), 1);
    }

    void theme_toggle()
    {
        const QString previous_style = qApp->styleSheet();
        const QVariant previous_mode = qApp->property("darkMode");
        DEFER(qApp->setStyleSheet(previous_style); qApp->setProperty("darkMode", previous_mode));
        QSettings settings("MusicPlayer", "MusicPlayer");
        settings.setValue("appearance/dark", false);

        playlist_window window;
        window.show();
        auto* button = window.findChild<QPushButton*>("themeButton");
        QVERIFY(button != nullptr);
        QVERIFY(!qApp->property("darkMode").toBool());
        QCOMPARE(button->toolTip(), QString("切换到暗色模式"));
        button->click();
        QVERIFY(qApp->property("darkMode").toBool());
        QCOMPARE(button->toolTip(), QString("切换到亮色模式"));
        QVERIFY(qApp->styleSheet().contains("#18222F"));
        QCOMPARE(settings.value("appearance/dark").toBool(), true);
        const QSize before = window.size();
        button->click();
        QVERIFY(!qApp->property("darkMode").toBool());
        QCOMPARE(window.size(), before);
        QCOMPARE(settings.value("appearance/dark").toBool(), false);
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
        auto* seek = window.findChild<QSlider*>("progressSlider");
        QVERIFY(seek != nullptr);
        QCOMPARE(seek->focusPolicy(), Qt::NoFocus);
        QTest::mouseClick(seek, Qt::LeftButton);
        QVERIFY(!seek->hasFocus());
        QVERIFY(qApp->styleSheet().contains("QSlider#progressSlider::add-page:horizontal"));
        auto* lyrics = window.findChild<lyrics_widget*>();
        QVERIFY(lyrics != nullptr);
        lyrics->set_lyrics({{0, "第一行中文歌词"}, {1000, "第二行中文歌词"}});
        QVERIFY(QMetaObject::invokeMethod(window.findChild<playback_controller*>(), "lyrics_updated",
                                          Q_ARG(QList<LyricLine>, QList<LyricLine>({{0, "第一行中文歌词"}}))));
        auto* lyrics_button = window.findChild<QPushButton*>("lyricsButton");
        QVERIFY(lyrics_button != nullptr);
        QVERIFY(!lyrics->isVisible());
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
        QVERIFY(tree != nullptr);
        const QRect original_list(tree->mapTo(&window, QPoint()), tree->size());
        QVERIFY(!lyrics_button->isCheckable());
        auto* panel = window.findChild<QWidget*>("lyricsPanel");
        QVERIFY(panel != nullptr);
        QVERIFY(!panel->isWindow());
        QVERIFY(window.findChild<QWidget*>("lyricsTitleBar") == nullptr);
        QVERIFY(window.findChild<QPushButton*>("lyricsWindowCloseButton") == nullptr);
        const QImage hidden_lyrics_icon = lyrics_button->icon().pixmap(20, 20).toImage();
        QVERIFY(!hidden_lyrics_icon.isNull());
        QCOMPARE(hidden_lyrics_icon, QIcon(":/icons/lyrics-hide.svg").pixmap(20, 20).toImage());
        QCOMPARE(lyrics_button->accessibleName(), QString("隐藏歌词"));
        lyrics_button->click();
        QVERIFY(lyrics->isVisible());
        const QImage visible_lyrics_icon = lyrics_button->icon().pixmap(20, 20).toImage();
        QVERIFY(!visible_lyrics_icon.isNull());
        QCOMPARE(visible_lyrics_icon, QIcon(":/icons/lyrics.svg").pixmap(20, 20).toImage());
        QVERIFY(visible_lyrics_icon != hidden_lyrics_icon);
        QCOMPARE(lyrics_button->accessibleName(), QString("显示歌词"));
        QCOMPARE(window.size(), QSize(340, 400));
        QVERIFY(window.rect().contains(QRect(panel->mapTo(&window, QPoint()), panel->size())));
        lyrics_button->click();
        QTRY_VERIFY(!lyrics->isVisible());
        QCOMPARE(window.size(), QSize(340, 400));
        QCOMPARE(lyrics_button->icon().pixmap(20, 20).toImage(), hidden_lyrics_icon);
        QTRY_COMPARE(QRect(tree->mapTo(&window, QPoint()), tree->size()), original_list);
        auto* mode = window.findChild<QPushButton*>("modeButton");
        auto* player = window.findChild<player_window*>();
        player->set_playback_mode(playback_mode::ListLoop);
        QSignalSpy changed(player, &player_window::playback_mode_changed);
        for (int i = 0; i < 4; ++i) QTest::mouseClick(mode, Qt::LeftButton);
        QCOMPARE(changed.count(), 4);
        QCOMPARE(mode->toolTip(), QString("列表循环"));
        QVERIFY(!mode->property("active").toBool());
        auto* add = window.findChild<QPushButton*>("addMusicButton");
        QVERIFY(add != nullptr);
        QVERIFY(add->isVisible());
    }

    void keyboard_shortcuts()
    {
        playlist_window window;
        auto* manager = window.findChild<playlist_manager*>();
        const qint64 id = manager->get_all_playlists().first().id;
        const QString second = temp_.filePath("shortcut-second.wav");
        QVERIFY(QFile::copy(tone_, second));
        QVERIFY(manager->add_songs_to_playlist(id, {tone_, second}));
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto* controller = window.findChild<playback_controller*>();
        auto* tree = window.findChild<QTreeWidget*>();
        QSignalSpy started(controller, &playback_controller::playback_started);
        QSignalSpy paused(controller, &playback_controller::playback_paused);
        QSignalSpy seek(controller, &playback_controller::seek_completed);
        QSignalSpy progress(controller, &playback_controller::progress_updated);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_playback_mode_changed", Q_ARG(playback_mode, playback_mode::ListLoop)));

        QTest::keyClick(&window, Qt::Key_Space);
        QCOMPARE(started.count(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty(), 1000);
        QTest::keyClick(&window, Qt::Key_Space);
        QCOMPARE(paused.count(), 1);
        QVERIFY(paused.last().first().toBool());

        QTest::keyClick(&window, Qt::Key_Right);
        QTRY_COMPARE_WITH_TIMEOUT(seek.count(), 1, 1000);
        const qint64 forward = seek.last().first().toLongLong();
        QVERIFY(forward >= 5000 && forward < 6500);
        QTest::keyClick(&window, Qt::Key_Left);
        QTRY_COMPARE_WITH_TIMEOUT(seek.count(), 2, 1000);
        QCOMPARE(seek.last().first().toLongLong(), forward - 5000);

        QTest::keyClick(&window, Qt::Key_Right, Qt::ControlModifier);
        QCOMPARE(started.count(), 2);
        QCOMPARE(started.last().first().toString(), second);
        QTest::keyClick(&window, Qt::Key_Left, Qt::ControlModifier);
        QCOMPARE(started.count(), 3);
        QCOMPARE(started.last().first().toString(), tone_);

        // 音量条获得焦点时方向键调整音量，不触发跳转。
        auto* volume = window.findChild<volume_meter*>();
        volume->setValue(50);
        volume->setFocus();
        QTest::keyClick(volume, Qt::Key_Right);
        QCOMPARE(volume->value(), 55);
        QCOMPARE(seek.count(), 2);

        tree->setFocus();
        auto* playlist = tree->topLevelItem(0);
        tree->setCurrentItem(playlist->child(1));
        QTest::keyClick(tree, Qt::Key_Return);
        QCOMPARE(started.count(), 4);
        QCOMPARE(started.last().first().toString(), second);
        tree->setCurrentItem(playlist);
        const bool expanded = playlist->isExpanded();
        QTest::keyClick(tree, Qt::Key_Return);
        QCOMPARE(playlist->isExpanded(), !expanded);

        tree->clearSelection();
        playlist->child(0)->setSelected(true);
        QTest::keyClick(tree, Qt::Key_Delete);
        QCOMPARE(manager->get_playlist_by_id(id).songs.size(), 1);
        QCOMPARE(manager->get_playlist_by_id(id).songs.first().file_path, second);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_stop_requested"));
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
        QList<LyricLine> lines{{0, "第一行"}, {2000, "第二行"}, {6000, "第三行"}};
        QVERIFY(QMetaObject::invokeMethod(controller, "lyrics_updated", Q_ARG(QList<LyricLine>, lines)));
        auto* lyrics_button = window.findChild<QPushButton*>("lyricsButton");
        QVERIFY(lyrics_button != nullptr);
        lyrics_button->click();
        QVERIFY(window.findChild<lyrics_widget*>()->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(progress.last().first().toLongLong() > before, 1000);
        controller->pause_resume();
        QSignalSpy seek(controller, &playback_controller::seek_completed);
        controller->seek(3000);
        QTRY_VERIFY_WITH_TIMEOUT(!seek.isEmpty(), 1000);
        auto* progress_slider = window.findChild<QSlider*>("progressSlider");
        QTRY_VERIFY_WITH_TIMEOUT(progress_slider->value() >= 2900, 1000);
        QCOMPARE(window.findChild<lyrics_widget*>()->accessibleDescription(), QString("第二行"));
        QVERIFY(window.findChild<QTreeWidget*>()->isVisible());
        controller->stop();
    }

    void slider_drag_preview()
    {
        playlist_window window;
        auto* manager = window.findChild<playlist_manager*>();
        QVERIFY(manager->add_songs_to_playlist(manager->get_all_playlists().first().id, {tone_}));
        window.show();
        auto* controller = window.findChild<playback_controller*>();
        QSignalSpy progress(controller, &playback_controller::progress_updated);
        QSignalSpy seek(controller, &playback_controller::seek_completed);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_play_requested"));
        QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty(), 1000);
        auto* slider = window.findChild<QSlider*>("progressSlider");
        auto* time_label = window.findChild<QLabel*>("timeLabel");

        slider->setSliderDown(true);
        slider->setSliderPosition(7000);
        QCOMPARE(time_label->text(), QString("00:07"));
        const int updates = progress.count();
        QTRY_VERIFY_WITH_TIMEOUT(progress.count() > updates + 1, 1000);
        QCOMPARE(time_label->text(), QString("00:07"));

        slider->setSliderDown(false);
        QTRY_COMPARE_WITH_TIMEOUT(seek.count(), 1, 1000);
        QVERIFY(seek.last().first().toLongLong() >= 7000);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_stop_requested"));
    }

    void title_bar_double_click()
    {
        playlist_window window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* title_bar = window.findChild<QWidget*>("appTitleBar");
        QTest::mouseDClick(title_bar, Qt::LeftButton, Qt::NoModifier, QPoint(150, 20));
        QTRY_VERIFY(window.isMaximized());
        QTest::mouseDClick(title_bar, Qt::LeftButton, Qt::NoModifier, QPoint(150, 20));
        QTRY_VERIFY(!window.isMaximized());
    }

    void window_resize_edges()
    {
        playlist_window window;
        window.show();
        auto* frame = window.centralWidget();
        QVERIFY(frame != nullptr);
        const auto handles = frame->findChildren<QWidget*>("windowResizeHandle", Qt::FindDirectChildrenOnly);
        QCOMPARE(handles.size(), 8);
        for (const QSize size : {QSize(540, 740), QSize(850, 530), QSize(340, 400)})
        {
            window.resize(size);
            QTest::qWait(50);
            QCOMPARE(window.size(), size);
            for (auto* handle : handles)
            {
                QVERIFY(handle->isVisible());
                QVERIFY(frame->rect().contains(handle->geometry()));
                QVERIFY(handle->width() > 0 && handle->height() > 0);
            }
        }
        QCOMPARE(handles.first()->geometry().topLeft(), QPoint(0, 0));
        QCOMPARE(handles.last()->geometry().bottomRight(), frame->rect().bottomRight());
    }

    void responsive_layout()
    {
        playlist_window window;
        window.show();
        auto* cover = window.findChild<QLabel*>("coverArtLabel");
        auto* slider = window.findChild<QSlider*>("progressSlider");
        for (const QSize size : {QSize(540, 740), QSize(360, 480), QSize(340, 400)})
        {
            window.resize(size);
            QTest::qWait(80);
            const QRect image(cover->mapTo(&window, QPoint()), cover->size());
            const QRect progress(slider->mapTo(&window, QPoint()), slider->size());
            QVERIFY(progress.left() > image.right());
            QVERIFY(progress.bottom() <= image.bottom());
            QVERIFY(window.rect().contains(image));
            QVERIFY(window.rect().contains(progress));
        }
    }

    void large_playlist_ui()
    {
        const auto old_style = qApp->styleSheet();
        DEFER(qApp->setStyleSheet(old_style));
        QFile style(":/style/stylesheet.qss");
        QVERIFY(style.open(QFile::ReadOnly));
        qApp->setStyleSheet(style.readAll());
        const QString short_file = temp_.filePath("list-short.wav");
        encode({"-i", tone_, "-t", "0.02", short_file});
        QStringList paths;
        for (int i = 0; i < 1200; ++i)
        {
            const auto path = temp_.filePath(QString("列表歌曲-%1.wav").arg(i));
            QVERIFY(QFile::copy(short_file, path));
            paths.append(path);
        }
        playlist_window window;
        window.show();
        auto* tree = window.findChild<QTreeWidget*>();
        auto* manager = window.findChild<playlist_manager*>();
        const auto id = manager->get_all_playlists().first().id;
        const int children = tree->viewport()->findChildren<QWidget*>().count();
        QElapsedTimer timer;
        timer.start();
        QVERIFY(manager->add_songs_to_playlist(id, paths));
        const auto import_ms = timer.elapsed();
        auto* group = tree->topLevelItem(0);
        QCOMPARE(group->childCount(), 1200);
        group->setExpanded(true);
        QTRY_COMPARE_WITH_TIMEOUT(pending_durations(), 0, 5000);
        const auto total_ms = timer.elapsed();
        qint64 max_interaction_ms = 0;
        for (int i = 0; i < 30; ++i)
        {
            timer.restart();
            tree->setCurrentItem(group->child((i * 41) % 1200));
            tree->scrollToItem(tree->currentItem());
            tree->viewport()->repaint();
            QCoreApplication::processEvents();
            max_interaction_ms = qMax(max_interaction_ms, timer.elapsed());
        }
        QCOMPARE(tree->viewport()->findChildren<QWidget*>().count(), children);
        QCOMPARE(group->child(0)->data(0, Qt::UserRole).toString(), paths.first());
        QVERIFY(group->child(0)->text(0).endsWith(".wav"));
        qInfo() << "1200首：导入同步(ms)" << import_ms << "含扫描(ms)" << total_ms
                << "最大选择滚动重绘(ms)" << max_interaction_ms << "逐行QWidget新增" << 0;
    }

    void svg_library_actions()
    {
        playlist_window window;
        window.show();
        auto* add_button = window.findChild<QPushButton*>("addMusicButton");
        QVERIFY(add_button != nullptr);
        QVERIFY(add_button->text().isEmpty());
        QVERIFY(add_button->toolTip().isEmpty());
        QVERIFY(!add_button->icon().isNull());
        QVERIFY(!add_button->accessibleName().isEmpty());
        const bool old = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        DEFER(QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, old));
        QTimer choose_file;
        connect(&choose_file, &QTimer::timeout, &window, [this]
        {
            auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if (dialog != nullptr && dialog->isVisible())
            {
                if (dialog->directory().absolutePath() != temp_.path()) dialog->setDirectory(temp_.path());
                auto* name = dialog->findChild<QLineEdit*>("fileNameEdit");
                if (name != nullptr) name->setText(tone_);
                QMetaObject::invokeMethod(dialog, "accept");
            }
        });
        choose_file.start(100);
        QTimer::singleShot(3000, &window, []
        {
            if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) dialog->reject();
        });
        window.findChild<QPushButton*>("addMusicButton")->click();
        choose_file.stop();
        auto* manager = window.findChild<playlist_manager*>();
        QCOMPARE(manager->get_playlist_by_id(manager->get_all_playlists().first().id).songs.size(), 1);
    }

    void lyrics_icon_states()
    {
        playlist_window window;
        window.show();
        auto* button = window.findChild<QPushButton*>("lyricsButton");
        auto* panel = window.findChild<QWidget*>("lyricsPanel");
        auto* lyrics = window.findChild<lyrics_widget*>();
        auto* controller = window.findChild<playback_controller*>();
        QVERIFY(button != nullptr);
        QVERIFY(panel != nullptr);
        QVERIFY(lyrics != nullptr);
        QVERIFY(controller != nullptr);

        const QImage unavailable_icon = QIcon(":/icons/lyrics-unavailable.svg").pixmap(20, 20).toImage();
        const QImage visible_icon = QIcon(":/icons/lyrics.svg").pixmap(20, 20).toImage();
        const QImage hidden_icon = QIcon(":/icons/lyrics-hide.svg").pixmap(20, 20).toImage();
        QVERIFY(!unavailable_icon.isNull());
        QVERIFY(!visible_icon.isNull());
        QVERIFY(!hidden_icon.isNull());
        QVERIFY(unavailable_icon != visible_icon);
        QVERIFY(unavailable_icon != hidden_icon);
        QVERIFY(visible_icon != hidden_icon);

        const auto expect_icon = [button](const QImage& expected, const QString& label, const QString& tip)
        {
            QCOMPARE(button->icon().pixmap(20, 20).toImage(), expected);
            QCOMPARE(button->accessibleName(), label);
            QCOMPARE(button->toolTip(), tip);
            QVERIFY(!button->isCheckable());
            QVERIFY(!button->isChecked());
        };

        expect_icon(unavailable_icon, "无歌词", "当前歌曲没有歌词，点击显示歌词");
        button->click();
        QVERIFY(panel->isVisible());
        expect_icon(unavailable_icon, "无歌词", "当前歌曲没有歌词，点击隐藏歌词");

        QList<LyricLine> lines{{0, "第一句"}, {2000, "第二句"}};
        QVERIFY(QMetaObject::invokeMethod(controller, "lyrics_updated", Q_ARG(QList<LyricLine>, lines)));
        QVERIFY(lyrics->has_lyrics());
        expect_icon(visible_icon, "显示歌词", "隐藏歌词");

        button->click();
        QTRY_VERIFY(!panel->isVisible());
        expect_icon(hidden_icon, "隐藏歌词", "显示歌词");

        lines.clear();
        QVERIFY(QMetaObject::invokeMethod(controller, "lyrics_updated", Q_ARG(QList<LyricLine>, lines)));
        QVERIFY(!lyrics->has_lyrics());
        expect_icon(unavailable_icon, "无歌词", "当前歌曲没有歌词，点击显示歌词");

        lines.append({0, "重新有歌词"});
        QVERIFY(QMetaObject::invokeMethod(controller, "lyrics_updated", Q_ARG(QList<LyricLine>, lines)));
        expect_icon(hidden_icon, "隐藏歌词", "显示歌词");

        button->click();
        QVERIFY(panel->isVisible());
        expect_icon(visible_icon, "显示歌词", "隐藏歌词");
        QVERIFY(QMetaObject::invokeMethod(&window, "on_stop_requested"));
        QVERIFY(!lyrics->has_lyrics());
        expect_icon(unavailable_icon, "无歌词", "当前歌曲没有歌词，点击隐藏歌词");
    }

    void adaptive_lyrics()
    {
        playlist_window window;
        window.show();
        auto* controller = window.findChild<playback_controller*>();
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
        auto* lyrics = window.findChild<lyrics_widget*>();
        auto* button = window.findChild<QPushButton*>("lyricsButton");
        auto* panel = window.findChild<QWidget*>("lyricsPanel");
        QVERIFY(controller != nullptr);
        QVERIFY(tree != nullptr);
        QVERIFY(lyrics != nullptr);
        QVERIFY(button != nullptr);
        QVERIFY(panel != nullptr);
        QVERIFY(!panel->isWindow());
        QVERIFY(!button->isCheckable());
        QVERIFY(!lyrics->isVisible());

        window.resize(960, 720);
        QTest::qWait(50);
        const QSize original_size = window.size();
        const QRect original_list(tree->mapTo(&window, QPoint()), tree->size());
        button->click();
        QVERIFY(lyrics->isVisible());
        QTRY_VERIFY(panel->width() >= 300);
        QCOMPARE(window.size(), original_size);
        QVERIFY(window.rect().contains(QRect(panel->mapTo(&window, QPoint()), panel->size())));
        QVERIFY(panel->mapTo(&window, QPoint()).x()
                >= tree->mapTo(&window, QPoint()).x() + tree->width());
        QVERIFY(tree->width() < original_list.width());
        QVERIFY(!button->isChecked());

        QList<LyricLine> lines{{0, "第一行"}, {2000, "第二行"}, {6000, "第三行"}};
        QVERIFY(QMetaObject::invokeMethod(controller, "lyrics_updated", Q_ARG(QList<LyricLine>, lines)));
        QVERIFY(lyrics->isVisible());
        lines.clear();
        QVERIFY(QMetaObject::invokeMethod(controller, "lyrics_updated", Q_ARG(QList<LyricLine>, lines)));
        QVERIFY(lyrics->isVisible());

        window.resize(540, 740);
        QTest::qWait(50);
        QCOMPARE(window.size(), QSize(540, 740));
        QVERIFY(window.rect().contains(QRect(panel->mapTo(&window, QPoint()), panel->size())));
        button->click();
        QTRY_VERIFY(!panel->isVisible());
        QCOMPARE(window.size(), QSize(540, 740));
        button->click();
        QVERIFY(panel->isVisible());
        QTRY_VERIFY(panel->width() > 0);
        QCOMPARE(window.size(), QSize(540, 740));
        QVERIFY(!button->isChecked());
    }

    void lyrics_integrated_layout()
    {
        playlist_window window;
        window.show();
        auto* panel = window.findChild<QWidget*>("lyricsPanel");
        auto* button = window.findChild<QPushButton*>("lyricsButton");
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
        auto* body = window.findChild<QWidget*>("playerBody");
        QVERIFY(panel != nullptr);
        QVERIFY(button != nullptr);
        QVERIFY(tree != nullptr);
        QVERIFY(body != nullptr);
        QVERIFY(!panel->isWindow());
        QCOMPARE(panel->parentWidget(), body);
        QVERIFY(window.findChild<QWidget*>("lyricsTitleBar") == nullptr);
        QVERIFY(window.findChild<QPushButton*>("lyricsWindowCloseButton") == nullptr);

        window.resize(1000, 720);
        QTest::qWait(50);
        const QSize size_before = window.size();
        const int list_width_before = tree->width();
        button->click();
        QVERIFY(panel->isVisible());
        QTRY_VERIFY(panel->width() >= 300);
        QCOMPARE(window.size(), size_before);
        QVERIFY(tree->width() < list_width_before);
        QVERIFY(body->rect().contains(QRect(panel->mapTo(body, QPoint()), panel->size())));
        QVERIFY(!button->isChecked());

        const QRect original_panel_position(panel->mapTo(&window, QPoint()), panel->size());
        window.move(window.pos() + QPoint(45, 25));
        QCOMPARE(QRect(panel->mapTo(&window, QPoint()), panel->size()), original_panel_position);
        QVERIFY(window.findChild<QPushButton*>("windowMaxButton") != nullptr);
        QVERIFY(window.findChild<QLabel*>("appLogo") != nullptr);

        button->click();
        QTRY_VERIFY(!panel->isVisible());
        QCOMPARE(window.size(), size_before);
        QTRY_COMPARE(tree->width(), list_width_before);
        button->click();
        QTRY_VERIFY(panel->width() >= 300);
        QCOMPARE(window.size(), size_before);
    }

    void lyrics_resize_during_animation()
    {
        playlist_window window;
        window.show();
        auto* panel = window.findChild<QWidget*>("lyricsPanel");
        auto* body = window.findChild<QWidget*>("playerBody");
        auto* button = window.findChild<QPushButton*>("lyricsButton");
        auto* animation = window.findChild<QPropertyAnimation*>();
        QVERIFY(panel != nullptr);
        QVERIFY(body != nullptr);
        QVERIFY(button != nullptr);
        QVERIFY(animation != nullptr);
        QCOMPARE(animation->targetObject(), static_cast<QObject*>(panel));

        window.resize(1000, 720);
        QTest::qWait(50);
        button->click();
        QCOMPARE(animation->state(), QAbstractAnimation::Running);
        window.resize(540, 720);
        QCOMPARE(window.size(), QSize(540, 720));
        QTRY_VERIFY(animation->state() == QAbstractAnimation::Stopped);
        QCOMPARE(panel->maximumWidth(), 200);
        QVERIFY(body->rect().contains(QRect(panel->mapTo(body, QPoint()), panel->size())));

        window.resize(340, 400);
        QCOMPARE(window.size(), QSize(340, 400));
        QCOMPARE(panel->maximumWidth(), 0);
    }

    void playback_state_restore_ui()
    {
        {
            playlist_window window;
            auto* manager = window.findChild<playlist_manager*>();
            const auto id = manager->get_all_playlists().first().id;
            QVERIFY(manager->add_songs_to_playlist(id, {tone_}));
            auto* controller = window.findChild<playback_controller*>();
            auto* tree = window.findChild<QTreeWidget*>();
            QSignalSpy progress(controller, &playback_controller::progress_updated);
            QVERIFY(QMetaObject::invokeMethod(&window, "on_tree_item_double_clicked",
                Q_ARG(QTreeWidgetItem*, tree->topLevelItem(0)->child(0)), Q_ARG(int, 0)));
            QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty(), 1000);
            controller->pause_resume();
            QSignalSpy seek(controller, &playback_controller::seek_finished);
            controller->seek(3000);
            QTRY_VERIFY_WITH_TIMEOUT(!seek.isEmpty(), 1000);
            QVERIFY(seek.last().first().toBool());
            auto* progress_slider = window.findChild<QSlider*>("progressSlider");
            QTRY_VERIFY_WITH_TIMEOUT(progress_slider->value() >= 2900, 1000);
            window.findChild<player_window*>()->set_playback_mode(playback_mode::Shuffle);
            auto* volume = window.findChild<volume_meter*>();
            volume->setValue(50);
            QTest::keyClick(volume, Qt::Key_End);
        }
        QSettings saved("MusicPlayer", "MusicPlayer");
        const int position = saved.value("playback/positionMs").toInt();
        QVERIFY(position >= 2900);
        playlist_window restored;
        QCOMPARE(restored.findChild<QSlider*>("progressSlider")->value(), position);
        QCOMPARE(restored.findChild<volume_meter*>()->value(), 100);
        QCOMPARE(restored.findChild<QPushButton*>("modeButton")->toolTip(), QString("随机播放"));
        QVERIFY(!restored.findChild<playback_controller*>()->is_media_loaded());
        QCOMPARE(restored.findChild<QTreeWidget*>()->currentItem()->data(0, Qt::UserRole).toString(), tone_);
    }

    void playback_state_saved_while_playing()
    {
        playlist_window window;
        auto* manager = window.findChild<playlist_manager*>();
        QVERIFY(manager->add_songs_to_playlist(manager->get_all_playlists().first().id, {tone_}));
        auto* controller = window.findChild<playback_controller*>();
        auto* tree = window.findChild<QTreeWidget*>();
        QSignalSpy progress(controller, &playback_controller::progress_updated);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_tree_item_double_clicked",
                                          Q_ARG(QTreeWidgetItem*, tree->topLevelItem(0)->child(0)), Q_ARG(int, 0)));
        QSettings saved("MusicPlayer", "MusicPlayer");
        QCOMPARE(saved.value("playback/filePath").toString(), tone_);
        QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty(), 1000);
        QSignalSpy seek(controller, &playback_controller::seek_completed);
        controller->seek(5000);
        QTRY_VERIFY_WITH_TIMEOUT(!seek.isEmpty(), 1000);
        // 进程异常退出时也不应丢失跳转后的位置。
        saved.sync();
        QVERIFY(saved.value("playback/positionMs").toLongLong() >= 4900);
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

    void missing_file_notice()
    {
        playback_controller controller;
        const QString missing = temp_.filePath("missing-notice.wav");
        QSignalSpy started(&controller, &playback_controller::playback_started);
        QSignalSpy errors(&controller, &playback_controller::playback_error);
        QSignalSpy unavailable(&controller, &playback_controller::file_unavailable);
        controller.play_file(missing);
        QCOMPARE(unavailable.count(), 1);
        QCOMPARE(unavailable.first().first().toString(), missing);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(started.count(), 0);
        QVERIFY(!controller.is_media_loaded());
    }

    void invalid_files()
    {
        const QString corrupt = temp_.filePath("corrupt.mp3");
        QFile file(corrupt);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("invalid audio");
        file.close();
        playback_controller controller;
        QSignalSpy errors(&controller, &playback_controller::playback_error);
        controller.play_file(corrupt);
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 1000);
        QCOMPARE(errors.first().first().toString(), QString("无法打开音频文件"));
        QVERIFY(!controller.is_media_loaded());
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
        QVERIFY(!manager.save_playlists(desired));
        QCOMPARE(database_snapshot(), before);
        QCOMPARE(changed.count(), 0);
        QVERIFY(query.exec("DROP TRIGGER injected_failure"));
        QVERIFY(manager.save_playlists(desired));
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
        QVERIFY(manager.save_playlists(desired));
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
        QTRY_COMPARE_WITH_TIMEOUT(probes, paths.size(), 5000);
        QTest::qWait(50);
        QCOMPARE(probes, paths.size());
        // 无法打开的文件保持未知时长，留待下次入队重新探测。
        QCOMPARE(pending_durations(), 2);
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

        QVERIFY(QFile::copy(short_file, paths.last()));
        QVERIFY(manager->add_songs_to_playlist(id, {}));
        QTRY_COMPARE_WITH_TIMEOUT(pending_durations(), 1, 1000);
        QCOMPARE(manager->get_playlist_by_id(id).songs[501].duration_ms, 20);
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
        database_manager verification;
        QVERIFY(verification.initialize());
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
        QSignalSpy decode_errors(controller, &playback_controller::playback_error);
        QSignalSpy unavailable(controller, &playback_controller::file_unavailable);
        const auto failures = [&] { return decode_errors.count() + unavailable.count(); };
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
            QCOMPARE(failures(), 4);
            QVERIFY(!stopped_dialog);
            QVERIFY(QMetaObject::invokeMethod(&window, "on_next_requested"));
        }
        QTRY_VERIFY_WITH_TIMEOUT(stopped_dialog, 1800);
        const int expected_errors = successful_track ? 4 + kFailureLimit : kFailureLimit;
        QCOMPARE(failures(), expected_errors);
        QVERIFY(!controller->is_media_loaded());
        QTest::qWait(350);
        QCOMPARE(failures(), expected_errors);
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
        QVERIFY(QMetaObject::invokeMethod(&window, "on_playback_mode_changed", Q_ARG(playback_mode, playback_mode::ListLoop)));
        QSignalSpy transitions(controller, &playback_controller::playback_started);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_tree_item_double_clicked", Q_ARG(QTreeWidgetItem*, first), Q_ARG(int, 0)));
        QTRY_COMPARE_WITH_TIMEOUT(transitions.count(), 1, 3000);
        QCOMPARE(transitions.at(0).at(0).toString(), tone_);
        QCOMPARE(tree->currentItem(), first);
        QVERIFY(first->isSelected());
        list->setExpanded(false);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_next_requested"));
        QTRY_COMPARE_WITH_TIMEOUT(transitions.count(), 2, 3000);
        QCOMPARE(transitions.at(1).at(0).toString(), second_path);
        QCOMPARE(tree->currentItem(), second);
        QVERIFY(second->isSelected());
        QVERIFY(QMetaObject::invokeMethod(&window, "on_next_requested"));
        QTRY_COMPARE_WITH_TIMEOUT(transitions.count(), 3, 3000);
        QCOMPARE(transitions.at(2).at(0).toString(), tone_);
        QCOMPARE(tree->currentItem(), first);
        QVERIFY(first->isSelected());
        QVERIFY(QMetaObject::invokeMethod(&window, "on_previous_requested"));
        QTRY_COMPARE_WITH_TIMEOUT(transitions.count(), 4, 3000);
        QCOMPARE(transitions.at(3).at(0).toString(), second_path);
        QCOMPARE(tree->currentItem(), second);
        QVERIFY(second->isSelected());
        QVERIFY(QMetaObject::invokeMethod(&window, "on_playback_mode_changed", Q_ARG(playback_mode, playback_mode::SingleLoop)));
        QSignalSpy started(controller, &playback_controller::playback_started);
        QVERIFY(QMetaObject::invokeMethod(&window, "handle_playback_finished"));
        QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 3000);
        QCOMPARE(started.first().first().toString(), second_path);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_playback_mode_changed", Q_ARG(playback_mode, playback_mode::Sequential)));
        QTRY_VERIFY_WITH_TIMEOUT(controller->is_media_loaded(), 3000);
        // 顺序播放到末尾时，手动下一首保持当前播放；只有自然播完才停止。
        QVERIFY(QMetaObject::invokeMethod(&window, "on_next_requested"));
        QVERIFY(controller->is_media_loaded());
        QCOMPARE(started.count(), 1);
        QCOMPARE(tree->currentItem(), second);
        QVERIFY(QMetaObject::invokeMethod(&window, "handle_playback_finished"));
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

    void shuffle_new_round()
    {
        playlist_window window;
        auto* manager = window.findChild<playlist_manager*>();
        const qint64 id = manager->get_all_playlists().first().id;
        const QString second = temp_.filePath("round-second.wav");
        QVERIFY(QFile::copy(tone_, second));
        QVERIFY(manager->add_songs_to_playlist(id, {tone_, second}));
        auto* controller = window.findChild<playback_controller*>();
        auto* tree = window.findChild<QTreeWidget*>();
        QVERIFY(QMetaObject::invokeMethod(&window, "on_playback_mode_changed", Q_ARG(playback_mode, playback_mode::Shuffle)));
        QVERIFY(QMetaObject::invokeMethod(&window, "on_tree_item_double_clicked",
                                          Q_ARG(QTreeWidgetItem*, tree->topLevelItem(0)->child(0)), Q_ARG(int, 0)));
        QSignalSpy started(controller, &playback_controller::playback_started);
        QString previous = tone_;
        // 两首歌时每一轮重洗都有一半概率把刚播的歌排在新一轮开头。
        for (int i = 0; i < 30; ++i)
        {
            QVERIFY(QMetaObject::invokeMethod(&window, "on_next_requested"));
            QCOMPARE(started.count(), i + 1);
            const QString current = started.last().first().toString();
            QVERIFY(current != previous);
            previous = current;
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
        const QString second = temp_.filePath("remove-song-second.wav");
        QVERIFY(QFile::copy(tone_, second));
        manager->add_songs_to_playlist(playlist.id, {tone_, second});
        auto* tree = window.findChild<QTreeWidget*>();
        auto* song = tree->topLevelItem(0)->child(0);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_playback_mode_changed", Q_ARG(playback_mode, playback_mode::ListLoop)));
        QVERIFY(QMetaObject::invokeMethod(&window, "on_tree_item_double_clicked", Q_ARG(QTreeWidgetItem*, song), Q_ARG(int, 0)));
        auto* controller = window.findChild<playback_controller*>();
        QTRY_VERIFY_WITH_TIMEOUT(controller->is_media_loaded(), 1000);
        manager->remove_songs_from_playlist(playlist.id, {0});
        // 移除正在播放的歌曲不打断播放，下一首从原位置继续。
        QVERIFY(controller->is_media_loaded());
        QSignalSpy started(controller, &playback_controller::playback_started);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_next_requested"));
        QCOMPARE(started.count(), 1);
        QCOMPARE(started.first().first().toString(), second);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_stop_requested"));
    }

    void move_playing_song()
    {
        playlist_window window;
        auto* manager = window.findChild<playlist_manager*>();
        QVERIFY(manager->create_new_playlist("目标"));
        const auto lists = manager->get_all_playlists();
        const qint64 source_id = lists.first().id;
        const qint64 target_id = lists.last().id;
        const QString kept = temp_.filePath("move-playing-kept.wav");
        const QString target_song = temp_.filePath("move-playing-target.wav");
        QVERIFY(QFile::copy(tone_, kept));
        QVERIFY(QFile::copy(tone_, target_song));
        QVERIFY(manager->add_songs_to_playlist(source_id, {tone_, kept}));
        QVERIFY(manager->add_songs_to_playlist(target_id, {target_song}));
        auto* tree = window.findChild<QTreeWidget*>();
        auto* controller = window.findChild<playback_controller*>();
        QVERIFY(QMetaObject::invokeMethod(&window, "on_playback_mode_changed", Q_ARG(playback_mode, playback_mode::ListLoop)));
        QVERIFY(QMetaObject::invokeMethod(&window, "on_tree_item_double_clicked",
                                          Q_ARG(QTreeWidgetItem*, tree->topLevelItem(0)->child(0)), Q_ARG(int, 0)));
        QTRY_VERIFY_WITH_TIMEOUT(controller->is_media_loaded(), 1000);

        QMap<qint64, Playlist> desired{{source_id, manager->get_playlist_by_id(source_id)},
                                     {target_id, manager->get_playlist_by_id(target_id)}};
        desired[target_id].songs.append(desired[source_id].songs.takeFirst());
        QVERIFY(manager->save_playlists(desired));
        QVERIFY(controller->is_media_loaded());

        // 当前歌曲已在目标歌单末尾，列表循环的下一首回到目标歌单开头。
        QSignalSpy started(controller, &playback_controller::playback_started);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_next_requested"));
        QCOMPARE(started.count(), 1);
        QCOMPARE(started.first().first().toString(), target_song);
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

#ifdef main
#error "SDL headers must not redefine the Qt Test main entry point"
#endif

QTEST_MAIN(playback_tests)
#include "playback_tests.moc"
