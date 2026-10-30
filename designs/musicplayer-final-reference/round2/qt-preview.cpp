#include <QPainter>
#include <QLinearGradient>
#include <QtTest>
#include <QTreeWidget>
#include <QPushButton>
#include <QTabBar>
#include <QSlider>
#include <QLabel>
#include <QBuffer>
#include <QJsonObject>
#include <QJsonDocument>
#include "playlist_window.h"
#include "playlist_manager.h"
#include "playback_controller.h"
#include "player_window.h"
#include "lyrics_widget.h"
#include "log.h"
int main(int argc,char** argv) {
 if(qgetenv("DISPLAY")!=":97" || !qgetenv("XDG_DATA_HOME").startsWith("/tmp/musicplayer-ui2/")) return 2;
 QApplication app(argc,argv); spdlog::set_level(spdlog::level::off);
 QFile style(":/style/stylesheet.qss");style.open(QIODevice::ReadOnly);app.setStyleSheet(style.readAll());
 app.setWindowIcon(QIcon(":/icons/app_icon.svg"));
 playlist_window w;w.show();auto* manager=w.findChild<playlist_manager*>();auto* player=w.findChild<player_window*>();auto* controller=w.findChild<playback_controller*>();auto* tree=w.findChild<QTreeWidget*>();auto* tabs=w.findChild<QTabBar*>("libraryTabs");
 auto capture=[&](QString name,QSize size){w.resize(size);QTest::qWait(150);w.grab().save(QString::fromLocal8Bit(argv[1])+"/"+name+".png");qInfo()<<name<<w.size();};
 capture("empty",{540,740});
 const auto id=manager->get_all_playlists().first().id;manager->rename_playlist(id,"粤语与经典");manager->create_new_playlist("午后散步");
 QStringList files;for(auto name:{"风继续吹","一生何求","光辉岁月","海阔天空","友情岁月","红日","永远的微笑","讲不出再见"}){QString path=QString("/tmp/musicplayer-ui2/%1.wav").arg(name);if(!QFile::exists(path))QFile::copy("/tmp/musicplayer-d/tone.wav",path);files<<path;}
 manager->add_songs_to_playlist(id,files);QTest::qWait(300);auto* first=tree->topLevelItem(0)->child(0);QMetaObject::invokeMethod(&w,"on_tree_item_double_clicked",Q_ARG(QTreeWidgetItem*,first),Q_ARG(int,0));QTest::qWait(200);controller->pause_resume();
 player->on_metadata_updated({{"title","风继续吹"},{"artist","张国荣"},{"album","风继续吹"},{"date","1983"}});
 QImage cover(160,160,QImage::Format_RGB32);cover.fill(QColor("#427BB3"));QPainter painter(&cover);QLinearGradient sky(0,0,0,160);sky.setColorAt(0,QColor("#75BAE8"));sky.setColorAt(.5,QColor("#E5ACAC"));sky.setColorAt(1,QColor("#244971"));painter.fillRect(cover.rect(),sky);painter.setBrush(QColor("#335D87"));painter.setPen(Qt::NoPen);painter.drawPolygon(QPolygon({{0,80},{40,40},{90,100},{130,65},{160,90},{160,115},{0,115}}));painter.end();QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);cover.save(&buffer,"PNG");player->on_cover_art_updated(bytes);
 capture("normal",{540,740});
 tree->setCurrentItem(tree->topLevelItem(0)->child(2));tree->setFocus();QTest::mouseMove(tree->viewport(),tree->visualItemRect(tree->topLevelItem(0)->child(3)).center());capture("list-states",{540,740});
 QTest::mouseClick(tree->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(6,tree->visualItemRect(tree->topLevelItem(0)).center().y()));qInfo()<<"branch after click"<<tree->topLevelItem(0)->isExpanded();
 QTest::mouseClick(tree->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(6,tree->visualItemRect(tree->topLevelItem(0)).center().y()));qInfo()<<"branch after second click"<<tree->topLevelItem(0)->isExpanded();
 capture("compact",{360,480});capture("minimum",{340,400});controller->pause_resume();capture("playing",{540,740});controller->pause_resume();player->on_cover_art_updated({});capture("no-cover",{540,740});player->on_metadata_updated({{"title","一首非常长的中文歌曲名称 — A very long filename with punctuation and Unicode"},{"artist","张国荣"}});capture("long",{360,480});
 QList<LyricLine> lines{{0,"让晚风轻轻吹送了落霞"},{1000,"我已习惯每个傍晚去想她"},{5000,"在远方的她"}};QMetaObject::invokeMethod(controller,"lyrics_updated",Q_ARG(QList<LyricLine>,lines));tabs->setCurrentIndex(1);capture("lyrics",{540,740});tabs->setCurrentIndex(0);
 QMetaObject::invokeMethod(&w,"on_manage_playlists_action");capture("management",{800,480});QMetaObject::invokeMethod(&w,"on_manage_playlists_action");if (argc > 2) {
 QString local = QString::fromLocal8Bit(argv[2]);
 manager->add_songs_to_playlist(id,{local});
 auto* group=tree->topLevelItem(0);
 auto* item=group->child(group->childCount()-1);
 QMetaObject::invokeMethod(&w,"on_tree_item_double_clicked",Q_ARG(QTreeWidgetItem*,item),Q_ARG(int,0));
 QTest::qWait(300);capture("real-local",{540,740});
 }
 controller->stop();return 0;
}
