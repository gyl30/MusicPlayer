#include <QtTest>
#include <QApplication>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QTreeWidget>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QBuffer>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include "playlist_window.h"
#include "playlist_manager.h"
#include "playback_controller.h"
#include "player_window.h"
#include "lyrics_widget.h"
#include "volumemeter.h"
#include "tray_icon.h"
#include "log.h"
static void report(const QString& event, QJsonObject value={})
{
    value.insert("event",event);
    qInfo().noquote()<<QJsonDocument(value).toJson(QJsonDocument::Compact);
}
static void capture(playlist_window& window,const QString& name)
{
    const QString output=QString::fromUtf8(qgetenv("MUSICPLAYER_D_OUTPUT"));
    window.grab().save(output+"/"+name+".png");
    QJsonArray outside;
    for (auto* button : window.findChildren<QPushButton*>())
    {
        if (!button->isVisible()) { continue; }
        const QRect rectangle(button->mapTo(&window,QPoint()),button->size());
        if (!window.rect().contains(rectangle)) { outside.append(button->objectName()); }
    }
    report(name,{{"width",window.width()},{"height",window.height()},{"dpr",window.devicePixelRatioF()},
                 {"min_width",window.minimumSizeHint().width()},{"buttons_outside",outside}});
}
int main(int argc,char** argv)
{
    if (!QString::fromUtf8(qgetenv("XDG_DATA_HOME")).startsWith("/tmp/musicplayer-d/")) { return 2; }
    if (qgetenv("DISPLAY") != ":97") { return 2; }
    QApplication app(argc,argv);
    spdlog::set_level(spdlog::level::warn);
    QFile style(":/style/stylesheet.qss");
    if (style.open(QIODevice::ReadOnly)) { app.setStyleSheet(QString::fromUtf8(style.readAll())); }
    app.setWindowIcon(QIcon(":/icons/app_icon.svg"));
    const QString mode=argc>1 ? QString::fromLocal8Bit(argv[1]) : "ui";
    const QString audio=argc>2 ? QString::fromLocal8Bit(argv[2]) : "/tmp/musicplayer-d/tone.wav";
    report("platform",{{"plugin",QGuiApplication::platformName()},{"tray_available",QSystemTrayIcon::isSystemTrayAvailable()},
                       {"logical_dpi",app.primaryScreen()->logicalDotsPerInch()}});
    if (mode=="exitclose")
    {
        playlist_window window;
        window.show();
        QTimer::singleShot(200, &window, [&]
        {
            const bool accepted=window.close();
            report("native_close",{{"accepted",accepted},{"tray_available",QSystemTrayIcon::isSystemTrayAvailable()}});
            if (!accepted)
            {
                QTimer::singleShot(100, &window, [&]{QMetaObject::invokeMethod(&window,"quit_application");});
            }
        });
        QTimer::singleShot(2000, &app, [&]{app.exit(99);});
        const int result=app.exec();
        report("event_loop_exit",{{"code",result}});
        return result;
    }
    {
        playlist_window window;
        auto* manager=window.findChild<playlist_manager*>();
        auto* controller=window.findChild<playback_controller*>();
        auto* player=window.findChild<player_window*>();
        auto* tree=window.findChild<QTreeWidget*>();
        auto* meter=window.findChild<volume_meter*>();
        auto* slider=window.findChild<QSlider*>("progressSlider");
        window.show();
        QTest::qWait(200);
        if (mode=="restore")
        {
            QSettings settings("MusicPlayer","MusicPlayer");
            report("restored",{{"file",settings.value("playback/filePath").toString()},
                               {"saved_position",settings.value("playback/positionMs").toLongLong()},
                               {"slider",slider->value()},{"saved_mode",settings.value("playback/mode").toInt()},
                               {"volume",meter->value()},{"loaded",controller->is_media_loaded()}});
            capture(window,"restore");
            return 0;
        }
        capture(window,"empty");
        manager->create_new_playlist("午后散步");
        const qint64 id=manager->get_all_playlists().first().id;
        manager->add_songs_to_playlist(id,{audio});
        QMetaObject::invokeMethod(&window,"on_tree_item_double_clicked",Q_ARG(QTreeWidgetItem*,tree->topLevelItem(0)->child(0)),Q_ARG(int,0));
        QTest::qWait(300);
        controller->seek(3210);
        QTest::qWait(200);
        controller->pause_resume();
        QTest::qWait(100);
        QTest::mouseClick(meter,Qt::LeftButton,Qt::NoModifier,QPoint(26,meter->height()/2));
        player->set_playback_mode(playback_mode::Shuffle);
        player->on_metadata_updated({{"title",QString("很长的中文歌曲名称与 Latin punctuation — %1").arg(QString(100,'A'))},{"artist","测试歌手"}});
        QImage image(128,64,QImage::Format_RGB32); image.fill(QColor(255,180,30));
        QByteArray cover; QBuffer buffer(&cover); buffer.open(QIODevice::WriteOnly); image.save(&buffer,"PNG");
        player->on_cover_art_updated(cover);
        QList<LyricLine> lyrics{{0,"第一行中文歌词，验证正常显示"},{1500,"第二行较长的歌词文字与长标题同时出现"},{4000,"第三行歌词"}};
        QMetaObject::invokeMethod(controller,"lyrics_updated",Q_ARG(QList<LyricLine>,lyrics));
        QTest::qWait(100);
        capture(window,"normal");
        player->on_metadata_updated({{"title","旅行的意义"},{"artist","陈绮贞"}});
        capture(window,"short-title");
        controller->pause_resume();
        QTest::qWait(200);
        capture(window,"playing");
        controller->pause_resume();
        player->handle_playback_error("文件已移动，请重新添加");
        capture(window,"error");
        window.resize(220,260);
        QTest::qWait(100);
        capture(window,"narrow");
        window.resize(500,650);
        QTest::qWait(100);
        capture(window,"wide");
        QMetaObject::invokeMethod(&window,"on_manage_playlists_action");
        QTest::qWait(100);
        capture(window,"management");
        window.resize(720,460);
        QTest::qWait(100);
        capture(window,"management-small");
        QMetaObject::invokeMethod(&window,"on_manage_playlists_action");
        window.showMinimized(); QTest::qWait(100);
        report("minimized",{{"minimized",window.isMinimized()}});
        window.showNormal(); QTest::qWait(100);
        const bool closed=window.close(); QTest::qWait(100);
        auto* tray=window.findChild<QSystemTrayIcon*>();
        report("close",{{"accepted",closed},{"window_visible",window.isVisible()},
                        {"tray_requested_visible",tray->isVisible()},{"tray_available",QSystemTrayIcon::isSystemTrayAvailable()}});
        window.show();
        QMetaObject::invokeMethod(window.findChild<tray_icon*>(),"show_hide_triggered");
        report("tray_hide",{{"visible",window.isVisible()}});
        QMetaObject::invokeMethod(window.findChild<tray_icon*>(),"show_hide_triggered");
        report("tray_show",{{"visible",window.isVisible()}});
        report("before_save",{{"position",slider->value()},{"volume",meter->value()}});
    }
    QSettings saved("MusicPlayer","MusicPlayer");
    report("saved",{{"position",saved.value("playback/positionMs").toLongLong()},
                    {"mode",saved.value("playback/mode").toInt()}, {"volume",saved.value("playback/volume").toInt()}});
}
