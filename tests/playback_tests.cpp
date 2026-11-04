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
#include "scrolling_text_label.h"
#include "music_library_view.h"
#include <atomic>
#include "audio_decoder.h"
#include "audio_player.h"
#include "playback_controller.h"
#include "playlist_manager.h"
#include "database_manager.h"
#include "playlist_window.h"
#include "song_scanner.h"
#include "scoped_exit.h"
#include "log.h"
#include <sstream>
#include <spdlog/sinks/ostream_sink.h>

extern "C"
{
#include <libavutil/log.h>
}

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

    // 点击“+”按钮，依次选择菜单项，并在随后的文件或文件夹对话框中选中 selection；返回各级菜单的项目文本。
    QList<QStringList> click_add_button(playlist_window& window, const QStringList& choices, const QString& selection)
    {
        const bool old = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        DEFER(QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, old));
        QList<QStringList> menus;
        QTimer interact;
        connect(&interact, &QTimer::timeout, &window, [&]
        {
            auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if (dialog != nullptr && dialog->isVisible())
            {
                if (dialog->directory().absolutePath() != temp_.path()) dialog->setDirectory(temp_.path());
                auto* name = dialog->findChild<QLineEdit*>("fileNameEdit");
                if (name != nullptr) name->setText(selection);
                QMetaObject::invokeMethod(dialog, "accept");
                return;
            }
            if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget()))
            {
                if (menus.size() >= choices.size()) return;
                QStringList texts;
                for (auto* action : menu->actions()) texts.append(action->text());
                if (!menus.isEmpty() && menus.last() == texts) return;
                menus.append(texts);
                for (auto* action : menu->actions())
                {
                    if (action->text() == choices.value(menus.size() - 1)) menu->setActiveAction(action);
                }
                QTest::keyClick(menu, Qt::Key_Return);
            }
        });
        interact.start(100);
        QTimer::singleShot(4000, &window, []
        {
            while (auto* popup = QApplication::activePopupWidget()) popup->close();
            if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) dialog->reject();
        });
        window.findChild<QPushButton*>("addMusicButton")->click();
        return menus;
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

    void library_store()
    {
        playlist_manager manager;
        manager.initialize_and_load();
        const qint64 first = manager.get_all_playlists().first().id;
        QVERIFY(manager.create_new_playlist("目标"));
        const qint64 target = manager.get_all_playlists().last().id;
        const QString a = temp_.filePath("library-a.wav");
        const QString b = temp_.filePath("library-b.wav");
        const QString c = temp_.filePath("library-c.wav");
        QVERIFY(manager.add_songs_to_playlist(first, {a, b}));
        QVERIFY(manager.add_songs_to_playlist(target, {b}));
        const auto find = [&](const QString& path)
        {
            for (const auto& entry : manager.get_library_songs())
            {
                if (entry.song.file_path == path) return entry;
            }
            return LibrarySong{};
        };

        // 从歌单移除不影响音乐库。
        QVERIFY(manager.remove_songs_from_playlist(first, {0}));
        QCOMPARE(find(a).song.file_path, a);
        QVERIFY(find(a).playlists.isEmpty());
        QStringList b_lists = find(b).playlists;
        b_lists.sort();
        QStringList expected{"默认列表", "目标"};
        expected.sort();
        QCOMPARE(b_lists, expected);

        QSignalSpy library_changed(&manager, &playlist_manager::library_changed);
        QVERIFY(manager.add_songs_to_library({c, a}));
        QCOMPARE(manager.get_library_songs().size(), 3);
        QVERIFY(find(c).playlists.isEmpty());
        QCOMPARE(library_changed.count(), 1);

        QSignalSpy changed(&manager, &playlist_manager::songs_changed_in_playlist);
        QVERIFY(manager.delete_songs_from_library({b}));
        QCOMPARE(manager.get_library_songs().size(), 2);
        QVERIFY(manager.get_playlist_by_id(first).songs.isEmpty());
        QVERIFY(manager.get_playlist_by_id(target).songs.isEmpty());
        QList<qint64> changed_ids;
        for (const auto& args : changed) changed_ids.append(args.first().toLongLong());
        std::sort(changed_ids.begin(), changed_ids.end());
        QCOMPARE(changed_ids, QList<qint64>({first, target}));
        QCOMPARE(library_changed.count(), 2);
    }

    void library_page()
    {
        playlist_window window;
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto* manager = window.findChild<playlist_manager*>();
        const qint64 first = manager->get_all_playlists().first().id;
        QVERIFY(manager->create_new_playlist("目标"));
        const qint64 target = manager->get_all_playlists().last().id;
        const QString extra = temp_.filePath("library-extra.wav");
        QVERIFY(QFile::copy(tone_, extra));
        const QString missing = temp_.filePath("library-missing.wav");
        QVERIFY(manager->add_songs_to_playlist(first, {tone_, missing}));
        QVERIFY(manager->add_songs_to_library({extra}));

        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
        auto* view = window.findChild<music_library_view*>("musicLibraryView");
        QVERIFY(view != nullptr);
        QVERIFY(!view->isVisible());
        window.findChild<QPushButton*>("libraryTabButton")->click();
        QVERIFY(view->isVisible());
        QVERIFY(!tree->isVisible());
        const auto row_paths = [&]
        {
            QStringList paths;
            for (int i = 0; i < view->topLevelItemCount(); ++i)
            {
                if (!view->topLevelItem(i)->isHidden()) paths.append(view->topLevelItem(i)->data(0, Qt::UserRole).toString());
            }
            return paths;
        };
        QCOMPARE(row_paths(), QStringList({extra, missing, tone_}));
        const auto select = [&](const QString& path)
        {
            view->clearSelection();
            for (int i = 0; i < view->topLevelItemCount(); ++i)
            {
                auto* item = view->topLevelItem(i);
                if (item->data(0, Qt::UserRole).toString() == path)
                {
                    item->setSelected(true);
                    view->setCurrentItem(item, 0, QItemSelectionModel::Select);
                }
            }
        };

        select(extra);
        QVERIFY(QMetaObject::invokeMethod(view, "add_selected_to_playlist", Q_ARG(qint64, target)));
        QCOMPARE(manager->get_playlist_by_id(target).songs.size(), 1);
        QCOMPARE(manager->get_playlist_by_id(target).songs.first().file_path, extra);

        QTest::keyClick(&window, Qt::Key_F, Qt::ControlModifier);
        QTest::keyClicks(window.findChild<QLineEdit*>("songSearchEdit"), "extra");
        QCOMPARE(row_paths(), QStringList({extra}));
        QTest::keyClick(window.findChild<QLineEdit*>("songSearchEdit"), Qt::Key_Escape);
        QCOMPARE(row_paths().size(), 3);

        // 从音乐库删除会同时移出所有歌单。
        select(missing);
        view->setFocus();
        QTimer confirm;
        accept_question_boxes(confirm, &window);
        QTest::keyClick(view, Qt::Key_Delete);
        confirm.stop();
        QCOMPARE(row_paths(), QStringList({extra, tone_}));
        QCOMPARE(manager->get_playlist_by_id(first).songs.size(), 1);
        QCOMPARE(tree->topLevelItem(0)->childCount(), 1);

        const QString gone = temp_.filePath("library-gone.wav");
        QVERIFY(manager->add_songs_to_library({gone}));
        QTRY_COMPARE(row_paths().size(), 3);
        QTimer confirm_cleanup;
        accept_question_boxes(confirm_cleanup, &window);
        QVERIFY(QMetaObject::invokeMethod(view, "on_cleanup_invalid_action"));
        confirm_cleanup.stop();
        QCOMPARE(row_paths(), QStringList({extra, tone_}));

        auto* controller = window.findChild<playback_controller*>();
        QSignalSpy started(controller, &playback_controller::playback_started);
        for (int i = 0; i < view->topLevelItemCount(); ++i)
        {
            auto* item = view->topLevelItem(i);
            if (item->data(0, Qt::UserRole).toString() == tone_) emit view->itemDoubleClicked(item, 0);
        }
        QCOMPARE(started.count(), 1);
        QCOMPARE(started.last().first().toString(), tone_);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_stop_requested"));

        window.findChild<QPushButton*>("playlistTabButton")->click();
        QVERIFY(tree->isVisible());
        QVERIFY(!view->isVisible());
    }

    void library_grouping()
    {
        const QString a1 = temp_.filePath("group-a1.mp3");
        const QString a2 = temp_.filePath("group-a2.mp3");
        const QString b1 = temp_.filePath("group-b1.mp3");
        const QString none = temp_.filePath("group-none.wav");
        encode({"-i", tone_, "-t", "1", "-metadata", "artist=歌手甲", "-metadata", "album=专辑一", a1});
        encode({"-i", tone_, "-t", "1", "-metadata", "artist=歌手甲", "-metadata", "album=专辑二", a2});
        encode({"-i", tone_, "-t", "1", "-metadata", "artist=Beta", "-metadata", "album=专辑一", b1});
        QVERIFY(QFile::copy(tone_, none));

        playlist_window window;
        window.show();
        auto* manager = window.findChild<playlist_manager*>();
        QVERIFY(manager->add_songs_to_library({a1, a2, b1, none}));
        const auto scanned = []
        {
            QSqlQuery query("SELECT COUNT(*) FROM Songs WHERE title IS NULL");
            return query.next() && query.value(0).toInt() == 0;
        };
        QTRY_VERIFY_WITH_TIMEOUT(scanned(), 3000);
        window.findChild<QPushButton*>("libraryTabButton")->click();
        auto* view = window.findChild<music_library_view*>("musicLibraryView");
        const auto top_texts = [&]
        {
            QStringList texts;
            for (int i = 0; i < view->topLevelItemCount(); ++i)
            {
                if (!view->topLevelItem(i)->isHidden()) texts.append(view->topLevelItem(i)->text(0));
            }
            return texts;
        };
        QTRY_COMPARE(top_texts(), QStringList({"group-a1.mp3", "group-a2.mp3", "group-b1.mp3", "group-none.wav"}));

        // 搜索同时匹配标签，列表仍显示文件名。
        view->set_filter("歌手甲");
        QCOMPARE(top_texts(), QStringList({"group-a1.mp3", "group-a2.mp3"}));
        view->set_filter("专辑一");
        QCOMPARE(top_texts(), QStringList({"group-a1.mp3", "group-b1.mp3"}));
        view->set_filter({});

        view->set_grouping(music_library_view::grouping::artist);
        QCOMPARE(top_texts(), QStringList({"Beta [1]", "歌手甲 [2]", "未知歌手 [1]"}));
        QVERIFY(!(view->topLevelItem(0)->flags() & Qt::ItemIsSelectable));
        view->set_filter("专辑二");
        QCOMPARE(top_texts(), QStringList({"歌手甲 [2]"}));
        view->focus_first_match();
        QCOMPARE(view->currentItem()->data(0, Qt::UserRole).toString(), a2);
        view->set_filter({});

        // 折叠的分组在刷新后保持折叠，里面的歌曲不参与全选后的批量操作。
        QTreeWidgetItem* group = view->topLevelItem(1);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualItemRect(group).center());
        QVERIFY(!group->isExpanded());
        QVERIFY(manager->create_new_playlist("刷新"));
        QVERIFY(!view->topLevelItem(1)->isExpanded());
        view->selectAll();
        const qint64 target = manager->get_all_playlists().last().id;
        view->add_selected_to_playlist(target);
        QStringList added;
        for (const auto& song : manager->get_playlist_by_id(target).songs) added.append(song.file_path);
        added.sort();
        QCOMPARE(added, QStringList({b1, none}));

        view->set_grouping(music_library_view::grouping::album);
        QCOMPARE(top_texts(), QStringList({"专辑一 [2]", "专辑二 [1]", "未知专辑 [1]"}));
        music_library_view restored(manager);
        QCOMPARE(QSettings("MusicPlayer", "MusicPlayer").value("library/grouping").toString(), QString("album"));
        restored.show();
        QCOMPARE(restored.topLevelItem(0)->text(0), QString("专辑一 [2]"));
        view->set_grouping(music_library_view::grouping::none);
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
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
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
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
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

    void song_search()
    {
        playlist_window window;
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto* manager = window.findChild<playlist_manager*>();
        const qint64 first_id = manager->get_all_playlists().first().id;
        QVERIFY(manager->create_new_playlist("其他"));
        QVERIFY(manager->create_new_playlist("空"));
        const qint64 other_id = manager->get_all_playlists().at(1).id;
        QVERIFY(manager->add_songs_to_playlist(first_id, {temp_.filePath("alpha.wav"), temp_.filePath("beta.wav")}));
        QVERIFY(manager->add_songs_to_playlist(other_id, {temp_.filePath("Alphabet.wav")}));
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
        auto* first = tree->topLevelItem(0);
        auto* other = tree->topLevelItem(1);
        auto* empty = tree->topLevelItem(2);
        other->setExpanded(true);
        auto* search = window.findChild<QLineEdit*>("songSearchEdit");
        QVERIFY(search != nullptr);
        QVERIFY(!search->isVisible());

        QTest::keyClick(&window, Qt::Key_F, Qt::ControlModifier);
        QVERIFY(search->isVisible());
        QVERIFY(search->hasFocus());
        QTest::keyClicks(search, "ALPHA");
        QVERIFY(!first->isHidden());
        QVERIFY(first->isExpanded());
        QVERIFY(!first->child(0)->isHidden());
        QVERIFY(first->child(1)->isHidden());
        QVERIFY(!other->isHidden());
        QVERIFY(empty->isHidden());

        // 过滤期间歌单刷新，新节点同样按关键字过滤。
        QVERIFY(manager->add_songs_to_playlist(first_id, {temp_.filePath("alpha2.wav"), temp_.filePath("gamma.wav")}));
        first = tree->topLevelItem(0);
        QCOMPARE(first->childCount(), 4);
        QVERIFY(!first->child(2)->isHidden());
        QVERIFY(first->child(3)->isHidden());

        QTest::keyClick(search, Qt::Key_Return);
        QCOMPARE(tree->currentItem(), first->child(0));
        QVERIFY(tree->hasFocus());

        search->setFocus();
        QTest::keyClick(search, Qt::Key_Escape);
        QVERIFY(!search->isVisible());
        QVERIFY(search->text().isEmpty());
        QVERIFY(!empty->isHidden());
        QVERIFY(!first->child(1)->isHidden());
        QVERIFY(!first->isExpanded());
        QVERIFY(other->isExpanded());
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
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
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

    void drop_external_files()
    {
        playlist_window window;
        window.resize(540, 740);
        window.show();
        auto* manager = window.findChild<playlist_manager*>();
        QVERIFY(manager->create_new_playlist("目标"));
        const qint64 first_id = manager->get_all_playlists().first().id;
        const qint64 target_id = manager->get_all_playlists().last().id;
        QDir root(temp_.path());
        QVERIFY(root.mkpath("drop/sub"));
        const QString nested = root.filePath("drop/sub/b.wav");
        const QString top = root.filePath("drop/a.WAV");
        const QString single = root.filePath("single.wav");
        for (const auto& path : {nested, top, single})
        {
            QVERIFY(QFile::copy(tone_, path));
        }
        QFile note(root.filePath("drop/readme.txt"));
        QVERIFY(note.open(QFile::WriteOnly));
        note.close();
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
        const QPoint target_row = tree->visualItemRect(tree->topLevelItem(1)).center();
        const QList<QUrl> urls{QUrl::fromLocalFile(root.filePath("drop")), QUrl::fromLocalFile(single)};
        QVERIFY(QMetaObject::invokeMethod(&window, "handle_file_drop", Q_ARG(QList<QUrl>, urls), Q_ARG(QPoint, target_row)));
        const auto paths = [&](qint64 id)
        {
            QStringList result;
            for (const auto& song : manager->get_playlist_by_id(id).songs) result.append(song.file_path);
            return result;
        };
        QTRY_COMPARE(paths(target_id), QStringList({top, nested, single}));
        QVERIFY(paths(first_id).isEmpty());
    }

    void transfer_failure()
    {
        playlist_window window;
        auto* manager = window.findChild<playlist_manager*>();
        QVERIFY(manager->create_new_playlist("目标"));
        const auto lists = manager->get_all_playlists();
        QVERIFY(manager->add_songs_to_playlist(lists.first().id, {tone_}));
        QTRY_COMPARE_WITH_TIMEOUT(pending_durations(), 0, 3000);
        window.findChild<QTreeWidget*>("songTreeWidget")->topLevelItem(0)->child(0)->setSelected(true);
        const auto before = database_snapshot();
        QSqlQuery query;
        QVERIFY(query.exec("CREATE TRIGGER reject_move BEFORE INSERT ON PlaylistSongs "
                           "BEGIN SELECT RAISE(ABORT, 'injected move failure'); END"));
        QTimer close_warning;
        accept_question_boxes(close_warning, &window);
        QVERIFY(QMetaObject::invokeMethod(&window, "on_move_songs_action", Q_ARG(qint64, lists.last().id)));
        close_warning.stop();
        QCOMPARE(database_snapshot(), before);
        QCOMPARE(window.findChild<QTreeWidget*>("songTreeWidget")->topLevelItem(0)->childCount(), 1);
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
        auto* volume = window.findChild<volume_meter*>();
        auto* lyrics = window.findChild<lyrics_widget*>();
        QCOMPARE(volume->property("trackColor").value<QColor>(), QColor("#34465C"));
        QCOMPARE(lyrics->property("normalColor").value<QColor>(), QColor("#8C9DB3"));
        QCOMPARE(lyrics->property("activeColor").value<QColor>(), QColor("#61ACFA"));
        // 为亮色背景设计的深色图标在暗色主题下要换成浅色，否则几乎看不见。
        const auto brightest = [](const QPixmap& pixmap)
        {
            const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
            int lightness = 0;
            for (int y = 0; y < image.height(); ++y)
            {
                for (int x = 0; x < image.width(); ++x)
                {
                    const QColor color = image.pixelColor(x, y);
                    if (color.alpha() > 200) lightness = qMax(lightness, color.lightness());
                }
            }
            return lightness;
        };
        const auto child_with_tip = [&window](const QString& tip) -> QWidget*
        {
            for (auto* widget : window.findChildren<QWidget*>())
            {
                if (widget->toolTip() == tip) return widget;
            }
            return nullptr;
        };
        auto* next = qobject_cast<QPushButton*>(child_with_tip("下一首"));
        auto* volume_icon = qobject_cast<QLabel*>(child_with_tip("音量"));
        QVERIFY(next != nullptr && volume_icon != nullptr);
        QVERIFY2(brightest(next->icon().pixmap(18, 18)) > 180, qPrintable(QString::number(brightest(next->icon().pixmap(18, 18)))));
        QVERIFY(brightest(volume_icon->pixmap()) > 150);
        const QSize before = window.size();
        button->click();
        QVERIFY(!qApp->property("darkMode").toBool());
        QCOMPARE(window.size(), before);
        QCOMPARE(settings.value("appearance/dark").toBool(), false);
        QCOMPARE(volume->property("trackColor").value<QColor>(), QColor("#D4DEEA"));
        QCOMPARE(lyrics->property("normalColor").value<QColor>(), QColor("#64758D"));
        QVERIFY(brightest(next->icon().pixmap(18, 18)) < 100);
        QVERIFY(brightest(volume_icon->pixmap()) < 150);
        QCOMPARE(lyrics->property("activeColor").value<QColor>(), QColor("#1478E8"));
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
        QCOMPARE(window.size(), QSize(340 + panel->width(), 400));
        QVERIFY(window.rect().contains(QRect(panel->mapTo(&window, QPoint()), panel->size())));
        QCOMPARE(QRect(tree->mapTo(&window, QPoint()), tree->size()), original_list);
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
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
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
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
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
        QVERIFY(window.findChild<QTreeWidget*>("songTreeWidget")->isVisible());
        QVERIFY(QMetaObject::invokeMethod(window.findChild<lyrics_widget*>(), "line_clicked", Q_ARG(qint64, 6000)));
        QTRY_COMPARE_WITH_TIMEOUT(seek.count(), 2, 1000);
        QVERIFY(seek.last().first().toLongLong() >= 6000);
        controller->stop();
    }

    void lyrics_manual_scroll()
    {
        lyrics_widget lyrics;
        lyrics.resize(300, 400);
        lyrics.show();
        QVERIFY(QTest::qWaitForWindowExposed(&lyrics));
        QList<LyricLine> lines;
        for (int i = 0; i < 30; ++i) lines.append({i * 1000, QString("第%1行").arg(i)});
        lyrics.set_lyrics(lines);
        lyrics.set_current_time(0);
        QSignalSpy clicked(&lyrics, &lyrics_widget::line_clicked);
        const QPoint center(150, 200);
        const auto line_at_center = [&]
        {
            clicked.clear();
            QTest::mouseClick(&lyrics, Qt::LeftButton, Qt::NoModifier, center);
            return clicked.isEmpty() ? qint64(-1) : clicked.last().first().toLongLong();
        };
        QTRY_COMPARE_WITH_TIMEOUT(line_at_center(), qint64(0), 2000);

        QWheelEvent wheel(QPointF(center), QPointF(lyrics.mapToGlobal(center)), {}, QPoint(0, -480), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&lyrics, &wheel);

        // 手动滚动后的短时间内，换行不应把视图拉回当前行；之后自动回到当前行。
        lyrics.set_current_time(20000);
        QCoreApplication::processEvents();
        const QImage held = lyrics.grab().toImage();
        QTest::qWait(1000);
        QCOMPARE(lyrics.grab().toImage(), held);
        QTRY_VERIFY_WITH_TIMEOUT(lyrics.grab().toImage() != held, 4000);
        QTRY_COMPARE_WITH_TIMEOUT(line_at_center(), qint64(20000), 3000);
    }

    void title_marquee()
    {
        QWidget host;
        auto* label = new scrolling_text_label(&host);
        const QString text("abcdefghijkl");
        const int text_width = QFontMetrics(label->font()).horizontalAdvance(text);
        label->setGeometry(0, 0, text_width - 10, QFontMetrics(label->font()).height() + 4);
        host.resize(label->size());
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        label->setText(text);
        QCoreApplication::processEvents();
        const QImage start = label->grab().toImage();

        // 先停留让用户读到开头，再滚动一圈后停在起点。
        QTest::qWait(800);
        QCOMPARE(label->grab().toImage(), start);
        QTRY_VERIFY_WITH_TIMEOUT(label->grab().toImage() != start, 2000);
        QTRY_VERIFY_WITH_TIMEOUT(label->grab().toImage() == start, 8000);
        QTest::qWait(600);
        QCOMPARE(label->grab().toImage(), start);

        QEnterEvent enter(QPointF(5, 5), QPointF(5, 5), label->mapToGlobal(QPointF(5, 5)));
        QCoreApplication::sendEvent(label, &enter);
        QTRY_VERIFY_WITH_TIMEOUT(label->grab().toImage() != start, 500);
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
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
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
        QCOMPARE(click_add_button(window, {"添加文件…"}, tone_), QList<QStringList>({{"添加文件…", "添加文件夹…"}}));
        auto* manager = window.findChild<playlist_manager*>();
        QCOMPARE(manager->get_playlist_by_id(manager->get_all_playlists().first().id).songs.size(), 1);
    }

    void add_button_target_menu()
    {
        playlist_window window;
        window.show();
        auto* manager = window.findChild<playlist_manager*>();
        QVERIFY(manager->create_new_playlist("目标"));
        const qint64 first_id = manager->get_all_playlists().first().id;
        const qint64 target_id = manager->get_all_playlists().last().id;
        const auto menus = click_add_button(window, {"添加文件…", "目标"}, tone_);
        QCOMPARE(menus, QList<QStringList>({{"添加文件…", "添加文件夹…"}, {"默认列表", "目标"}}));
        QCOMPARE(manager->get_playlist_by_id(target_id).songs.size(), 1);
        QCOMPARE(manager->get_playlist_by_id(first_id).songs.size(), 0);
    }

    void add_folder()
    {
        QDir root(temp_.filePath("add-folder"));
        QVERIFY(root.mkpath("disc 10"));
        QVERIFY(root.mkpath("disc 2"));
        const QString first = root.filePath("disc 2/a.wav");
        const QString second = root.filePath("disc 10/a.wav");
        const QString top = root.filePath("b.WAV");
        QVERIFY(QFile::copy(tone_, first));
        QVERIFY(QFile::copy(tone_, second));
        QVERIFY(QFile::copy(tone_, top));
        QFile note(root.filePath("cover.txt"));
        QVERIFY(note.open(QFile::WriteOnly));
        note.close();

        // Qt 6.2 的 QCollator 在 C 区域设置下忽略数字顺序，按用户常见的中文环境测试。
        const QLocale old_locale;
        QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
        DEFER(QLocale::setDefault(old_locale));
        playlist_window window;
        window.show();
        auto* manager = window.findChild<playlist_manager*>();
        const qint64 id = manager->get_all_playlists().first().id;
        click_add_button(window, {"添加文件夹…"}, root.absolutePath());
        QStringList added;
        for (const auto& song : manager->get_playlist_by_id(id).songs) added.append(song.file_path);
        // 递归包含子文件夹，按自然顺序排列，忽略非音频文件。
        QCOMPARE(added, QStringList({top, first, second}));

        const QString library_only = temp_.filePath("add-folder-library");
        QVERIFY(QDir().mkpath(library_only + "/sub"));
        QVERIFY(QFile::copy(tone_, library_only + "/sub/c.wav"));
        window.findChild<QPushButton*>("libraryTabButton")->click();
        click_add_button(window, {"添加文件夹…"}, library_only);
        bool imported = false;
        for (const auto& entry : manager->get_library_songs()) imported = imported || entry.song.file_path == library_only + "/sub/c.wav";
        QVERIFY(imported);
        QCOMPARE(manager->get_playlist_by_id(id).songs.size(), 3);
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

    void lyrics_popout()
    {
        playlist_window window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* panel = window.findChild<QWidget*>("lyricsPanel");
        auto* button = window.findChild<QPushButton*>("lyricsButton");
        auto* page = window.findChild<QWidget*>("playbackPage");
        auto* body = window.findChild<QWidget*>("playerBody");
        QVERIFY(panel != nullptr && button != nullptr && page != nullptr && body != nullptr);
        QVERIFY(!panel->isWindow());
        QCOMPARE(panel->parentWidget(), body);
        const QRect screen = window.screen()->availableGeometry();
        const auto page_rect = [&] { return QRect(page->mapTo(&window, QPoint()), page->size()); };
        const auto panel_rect = [&] { return QRect(panel->mapTo(&window, QPoint()), panel->size()); };

        // 有空间时窗口向右加宽，主界面位置和宽度不变。
        window.setGeometry(screen.left() + 10, screen.top() + 10, 400, qMin(500, screen.height() - 20));
        QTest::qWait(50);
        const QRect normal = window.geometry();
        const QRect page_before = page_rect();
        button->click();
        QVERIFY(panel->isVisible());
        QVERIFY(panel->width() >= 300);
        QTRY_COMPARE(window.geometry(), normal.adjusted(0, 0, panel->width(), 0));
        QTRY_COMPARE(page_rect(), page_before);
        QCOMPARE(panel_rect().left(), page_before.right() + 1);
        QVERIFY(window.rect().contains(panel_rect()));
        button->click();
        QTRY_VERIFY(!panel->isVisible());
        QTRY_COMPARE(window.geometry(), normal);
        QTRY_COMPARE(page_rect(), page_before);

        // 右侧空间不足时整体左移，关闭后回到原位。
        const QRect near_edge(screen.right() - 450, normal.top(), 400, normal.height());
        window.setGeometry(near_edge);
        QTest::qWait(50);
        button->click();
        QTRY_COMPARE(window.geometry().right(), screen.right());
        QCOMPARE(window.width(), 400 + panel->width());
        QTRY_COMPARE(page_rect().size(), page_before.size());
        button->click();
        QTRY_COMPARE(window.geometry(), near_edge);

        // 最大化时没有加宽余地，歌词以浮层盖在右侧，不压缩主界面。
        window.showMaximized();
        QTRY_VERIFY(window.isMaximized());
        QTest::qWait(50);
        const QSize maximized = window.size();
        const int page_width = page->width();
        button->click();
        QVERIFY(panel->isVisible());
        QTest::qWait(50);
        QCOMPARE(window.size(), maximized);
        QCOMPARE(page->width(), page_width);
        QCOMPARE(panel_rect().right(), QRect(body->mapTo(&window, QPoint()), body->size()).right());
        QVERIFY(window.rect().contains(panel_rect()));
        button->click();
        QTRY_VERIFY(!panel->isVisible());
        QCOMPARE(window.size(), maximized);
    }

    void playback_state_restore_ui()
    {
        {
            playlist_window window;
            auto* manager = window.findChild<playlist_manager*>();
            const auto id = manager->get_all_playlists().first().id;
            QVERIFY(manager->add_songs_to_playlist(id, {tone_}));
            auto* controller = window.findChild<playback_controller*>();
            auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
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
        QCOMPARE(restored.findChild<QTreeWidget*>("songTreeWidget")->currentItem()->data(0, Qt::UserRole).toString(), tone_);
    }

    void playback_state_saved_while_playing()
    {
        playlist_window window;
        auto* manager = window.findChild<playlist_manager*>();
        QVERIFY(manager->add_songs_to_playlist(manager->get_all_playlists().first().id, {tone_}));
        auto* controller = window.findChild<playback_controller*>();
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
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

    void folder_cover_art()
    {
        QByteArray cover;
        const auto load_cover = [&cover](const QString& audio)
        {
            cover.clear();
            playback_controller controller;
            QSignalSpy covers(&controller, &playback_controller::cover_art_ready);
            QSignalSpy metadata(&controller, &playback_controller::lyrics_updated);
            controller.play_file(audio);
            QTRY_VERIFY_WITH_TIMEOUT(!metadata.isEmpty(), 3000);
            QTest::qWait(100);
            controller.stop();
            if (!covers.isEmpty()) cover = covers.last().first().toByteArray();
        };
        const auto read = [](const QString& path)
        {
            QFile file(path);
            return file.open(QFile::ReadOnly) ? file.readAll() : QByteArray();
        };

        QDir dir(temp_.filePath("cover-album"));
        QVERIFY(dir.mkpath("."));
        const QString audio = dir.filePath("track.wav");
        QVERIFY(QFile::copy(tone_, audio));
        load_cover(audio);
        QVERIFY(cover.isEmpty());

        // 目录图片按 cover、folder、front、album 的顺序选用，文件名不区分大小写。
        encode({"-f", "lavfi", "-i", "color=c=blue:s=8x8", "-frames:v", "1", dir.filePath("Folder.JPG")});
        load_cover(audio);
        QCOMPARE(cover, read(dir.filePath("Folder.JPG")));
        encode({"-f", "lavfi", "-i", "color=c=red:s=8x8", "-frames:v", "1", dir.filePath("cover.png")});
        load_cover(audio);
        QCOMPARE(cover, read(dir.filePath("cover.png")));

        // 内嵌封面优先于目录图片。
        const QString picture = temp_.filePath("embedded-cover.png");
        encode({"-f", "lavfi", "-i", "color=c=green:s=8x8", "-frames:v", "1", picture});
        const QString embedded = dir.filePath("embedded.mp3");
        encode({"-i", tone_, "-i", picture, "-t", "1", "-map", "0:a", "-map", "1:v", "-c:v", "copy", "-disposition:v", "attached_pic", embedded});
        load_cover(embedded);
        QCOMPARE(cover, read(picture));
    }

    void external_lyrics()
    {
        QStringList lines;
        const auto load_lyrics = [&lines](const QString& audio)
        {
            lines.clear();
            playback_controller controller;
            QSignalSpy lyrics(&controller, &playback_controller::lyrics_updated);
            controller.play_file(audio);
            QTRY_VERIFY_WITH_TIMEOUT(!lyrics.isEmpty(), 3000);
            controller.stop();
            for (const auto& line : lyrics.last().first().value<QList<LyricLine>>()) lines.append(QString("%1 %2").arg(line.timestamp_ms).arg(line.text));
        };
        const auto write = [](const QString& path, const QByteArray& bytes)
        {
            QFile file(path);
            QVERIFY(file.open(QFile::WriteOnly));
            file.write(bytes);
        };

        // 常见的 GBK 编码 .lrc，扩展名大写。
        const QString gbk_audio = temp_.filePath("ext-gbk.wav");
        QVERIFY(QFile::copy(tone_, gbk_audio));
        write(temp_.filePath("ext-gbk.LRC"), QByteArray("[00:01.00]\xB5\xDA\xD2\xBB\xBE\xE4\n[00:02.50]\xB5\xDA\xB6\xFE\xBE\xE4\n"));
        load_lyrics(gbk_audio);
        QCOMPARE(lines, QStringList({"1000 第一句", "2500 第二句"}));

        // 带 BOM 的 UTF-8 .lrc 优先于内嵌歌词。
        const QString embedded = temp_.filePath("ext-embedded.flac");
        encode({"-i", tone_, "-t", "3", "-metadata", "lyrics=[00:00.50]内嵌歌词", embedded});
        load_lyrics(embedded);
        QCOMPARE(lines, QStringList({"500 内嵌歌词"}));
        write(temp_.filePath("ext-embedded.lrc"), QByteArray("\xEF\xBB\xBF[00:00.80]外置歌词\n"));
        load_lyrics(embedded);
        QCOMPARE(lines, QStringList({"800 外置歌词"}));

        // 外置文件没有带时间的歌词时，仍使用内嵌歌词。
        write(temp_.filePath("ext-embedded.lrc"), QByteArray("只有文字没有时间\n"));
        load_lyrics(embedded);
        QCOMPARE(lines, QStringList({"500 内嵌歌词"}));
    }

    void gbk_metadata()
    {
        // 老 MP3 常把 GBK 中文写进标为 ISO-8859-1 的 ID3 帧。
        const QString plain = temp_.filePath("gbk-plain.mp3");
        encode({"-i", tone_, "-t", "1", "-id3v2_version", "0", "-write_xing", "0", plain});
        const auto frame = [](const char* id, const QByteArray& text)
        {
            QByteArray body = QByteArray(1, '\0') + text;
            QByteArray result(id, 4);
            for (int shift = 24; shift >= 0; shift -= 8) result.append(static_cast<char>((body.size() >> shift) & 0xFF));
            return result + QByteArray(2, '\0') + body;
        };
        const QByteArray frames = frame("TIT2", QByteArray("\xB2\xE2\xCA\xD4\xB1\xEA\xCC\xE2"))
                                + frame("TPE1", QByteArray("\xB2\xE2\xCA\xD4\xB8\xE8\xCA\xD6"))
                                + frame("TALB", QByteArray("Caf\xE9"));
        QByteArray tag("ID3\x03\x00\x00", 6);
        for (int shift = 21; shift >= 0; shift -= 7) tag.append(static_cast<char>((frames.size() >> shift) & 0x7F));
        QFile source(plain);
        QVERIFY(source.open(QFile::ReadOnly));
        const QString tagged = temp_.filePath("gbk-tagged.mp3");
        QFile target(tagged);
        QVERIFY(target.open(QFile::WriteOnly));
        target.write(tag + frames + source.readAll());
        target.close();

        playback_controller controller;
        QSignalSpy metadata(&controller, &playback_controller::metadata_ready);
        controller.play_file(tagged);
        QTRY_VERIFY_WITH_TIMEOUT(!metadata.isEmpty(), 3000);
        const auto values = metadata.last().first().value<QMap<QString, QString>>();
        QCOMPARE(values.value("title"), QString("测试标题"));
        QCOMPARE(values.value("artist"), QString("测试歌手"));
        QCOMPARE(values.value("album"), QString("Café"));
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

    void decoder_open_failure_log()
    {
        // 没有音频流的文件以前只提示“无法打开音频文件”，日志里看不出失败在哪一步。
        const QString image = temp_.filePath("no-audio.png");
        encode({"-f", "lavfi", "-i", "color=c=red:s=16x16", "-frames:v", "1", image});
        std::ostringstream output;
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(output);
        auto& sinks = spdlog::default_logger()->sinks();
        sinks.push_back(sink);
        DEFER(sinks.pop_back());
        {
            playback_controller controller;
            QSignalSpy errors(&controller, &playback_controller::playback_error);
            controller.play_file(image);
            QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 3000);
        }
        const QString text = QString::fromStdString(output.str());
        QVERIFY2(text.contains("打开音频失败 session 1 file " + image + " step av_find_best_stream"), qPrintable(text));
        QVERIFY2(text.contains("解码器错误 session 1 error"), qPrintable(text));
    }

    void log_forwarding()
    {
        const auto previous = spdlog::default_logger();
        const QString path = temp_.filePath("forward.log");
        init_log(QFile::encodeName(path).toStdString());
        qWarning("转发测试警告");
        av_log(nullptr, AV_LOG_ERROR, "转发测试错误\n");
        av_log(nullptr, AV_LOG_INFO, "不应转发的信息\n");
        qInstallMessageHandler(nullptr);
        av_log_set_callback(av_log_default_callback);
        spdlog::set_default_logger(previous);
        spdlog::set_level(spdlog::level::warn);

        QFile file(path);
        QVERIFY(file.open(QFile::ReadOnly));
        const QString text = QString::fromUtf8(file.readAll());
        QVERIFY2(text.contains("qt 转发测试警告"), qPrintable(text));
        QVERIFY2(text.contains("ffmpeg 转发测试错误"), qPrintable(text));
        QVERIFY2(!text.contains("不应转发的信息"), qPrintable(text));
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
        connect(manager->findChild<song_scanner*>(), &song_scanner::songs_scanned, manager,
                [&](const QMap<QString, scanned_song>& values) { probes += static_cast<int>(values.size()); }, Qt::QueuedConnection);
        QVERIFY(manager->add_songs_to_playlist(id, paths));
        QVERIFY(pending_durations() > 0);
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
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
        connect(manager.findChild<song_scanner*>(), &song_scanner::songs_scanned, &manager,
                [&](const QMap<QString, scanned_song>& values) { probes += static_cast<int>(values.size()); }, Qt::QueuedConnection);
        QSignalSpy updates(&manager, &playlist_manager::songs_scanned);
        QVERIFY(manager.add_songs_to_playlist(id, {tone_, temp_.filePath("removed-missing.wav")}));
        QVERIFY(manager.delete_songs_from_library({tone_, temp_.filePath("removed-missing.wav")}));
        QTRY_COMPARE_WITH_TIMEOUT(probes, 2, 1000);
        QCOMPARE(updates.count(), 0);
        QSqlQuery query("SELECT COUNT(*) FROM Songs WHERE duration_ms IS NOT NULL");
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 0);
        QVERIFY(manager.add_songs_to_playlist(id, {tone_}));
        QTRY_COMPARE_WITH_TIMEOUT(pending_durations(), 0, 1000);
        QVERIFY(manager.get_playlist_by_id(id).songs.first().duration_ms > 0);
    }

    void library_only_scan()
    {
        // 只导入音乐库、不在任何歌单中的歌曲也要扫描出时长。
        playlist_manager manager;
        manager.initialize_and_load();
        const QString path = temp_.filePath("library-only.wav");
        QVERIFY(QFile::copy(tone_, path));
        QVERIFY(manager.add_songs_to_library({path}));
        QTRY_VERIFY_WITH_TIMEOUT(manager.get_library_songs().size() == 1 && manager.get_library_songs().first().song.duration_ms > 0, 3000);
    }

    void song_tags_scanned()
    {
        const QString tagged = temp_.filePath("tagged.mp3");
        encode({"-i", tone_, "-t", "1", "-metadata", "title=夜曲", "-metadata", "artist=周杰伦", "-metadata", "album=十一月的萧邦", tagged});
        const QString plain = temp_.filePath("plain.wav");
        QVERIFY(QFile::copy(tone_, plain));
        const auto song_of = [](const QList<LibrarySong>& songs, const QString& path)
        {
            for (const auto& entry : songs)
            {
                if (entry.song.file_path == path) return entry.song;
            }
            return Song{};
        };
        const auto unscanned = []
        {
            QSqlQuery query("SELECT COUNT(*) FROM Songs WHERE duration_ms IS NULL OR title IS NULL");
            return query.next() ? query.value(0).toInt() : -1;
        };
        {
            playlist_manager manager;
            manager.initialize_and_load();
            QVERIFY(manager.add_songs_to_library({tagged, plain}));
            QTRY_COMPARE_WITH_TIMEOUT(unscanned(), 0, 3000);
            const Song song = song_of(manager.get_library_songs(), tagged);
            QCOMPARE(song.title, QString("夜曲"));
            QCOMPARE(song.artist, QString("周杰伦"));
            QCOMPARE(song.album, QString("十一月的萧邦"));
            // 没有标签的文件记为已扫描，不会每次启动都重扫。
            QCOMPARE(song_of(manager.get_library_songs(), plain).artist, QString());
            // 模拟升级前只扫描过时长的老数据库。
            QSqlQuery reset;
            QVERIFY(reset.exec("UPDATE Songs SET title = NULL, artist = NULL, album = NULL"));
        }
        playlist_manager manager;
        manager.initialize_and_load();
        QTRY_COMPARE_WITH_TIMEOUT(unscanned(), 0, 3000);
        QCOMPARE(song_of(manager.get_library_songs(), tagged).artist, QString("周杰伦"));
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
        connect(manager.findChild<song_scanner*>(), &song_scanner::songs_scanned, &manager,
                [&](const QMap<QString, scanned_song>& values) { probes += static_cast<int>(values.size()); }, Qt::QueuedConnection);
        QSignalSpy updates(&manager, &playlist_manager::songs_scanned);
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
            auto* scanner = manager.findChild<song_scanner*>();
            connect(scanner, &song_scanner::songs_scanned, scanner, [&](const QMap<QString, scanned_song>&)
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

    void tray_hint_once()
    {
        if (!QSystemTrayIcon::isSystemTrayAvailable())
        {
            QSKIP("该用例需要系统托盘服务");
        }
        QApplication::setWindowIcon(QIcon(":/icons/app_icon.svg"));
        QSettings("MusicPlayer", "MusicPlayer").remove("ui/tray_hint_shown");
        const auto visible_balloons = []
        {
            int count = 0;
            for (auto* widget : QApplication::topLevelWidgets())
            {
                if (QByteArray(widget->metaObject()->className()) == "QBalloonTip" && widget->isVisible())
                {
                    ++count;
                }
            }
            return count;
        };
        playlist_window window;
        window.show();
        QTest::qWait(200);
        QVERIFY(!window.close());
        QVERIFY(!window.isVisible());
        QTRY_COMPARE(visible_balloons(), 1);
        QVERIFY(QSettings("MusicPlayer", "MusicPlayer").value("ui/tray_hint_shown").toBool());
        for (auto* widget : QApplication::topLevelWidgets())
        {
            if (QByteArray(widget->metaObject()->className()) == "QBalloonTip")
            {
                widget->close();
            }
        }
        QTRY_COMPARE(visible_balloons(), 0);

        window.show();
        QTest::qWait(50);
        QVERIFY(!window.close());
        QTest::qWait(300);
        QCOMPARE(visible_balloons(), 0);
    }

    void metadata_path_reuse()
    {
        playlist_manager manager;
        manager.initialize_and_load();
        const qint64 id = manager.get_all_playlists().first().id;
        int probes = 0;
        connect(manager.findChild<song_scanner*>(), &song_scanner::songs_scanned, &manager,
                [&](const QMap<QString, scanned_song>& values) { probes += static_cast<int>(values.size()); }, Qt::QueuedConnection);
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
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
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
        auto* list = window.findChild<QTreeWidget*>("songTreeWidget")->topLevelItem(0);
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
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
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
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
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
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
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
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
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
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
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
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
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
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
        auto* tree = window.findChild<QTreeWidget*>("songTreeWidget");
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
