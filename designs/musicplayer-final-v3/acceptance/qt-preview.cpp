#include <QtTest>
#include <QTreeWidget>
#include <QPushButton>
#include <QTabBar>
#include <QSlider>
#include <QLabel>
#include <QDirIterator>
#include <QMenu>
#include <QContextMenuEvent>
#include "playlist_window.h"
#include "playlist_manager.h"
#include "playback_controller.h"
#include "lyrics_widget.h"
#include "log.h"
int main(int argc,char** argv) {
 if(qgetenv("DISPLAY")!=":97" || !qgetenv("XDG_DATA_HOME").startsWith("/tmp/musicplayer-v3/"))return 2;
 QApplication app(argc,argv);spdlog::set_level(spdlog::level::off);QFile qss(":/style/stylesheet.qss");qss.open(QIODevice::ReadOnly);app.setStyleSheet(qss.readAll());app.setWindowIcon(QIcon(":/icons/app_icon.svg"));
 playlist_window w;w.show();auto* manager=w.findChild<playlist_manager*>();auto* controller=w.findChild<playback_controller*>();auto* tree=w.findChild<QTreeWidget*>();
 auto shot=[&](QString name,QSize size){w.resize(size);QTest::qWait(550);w.grab().save(QString::fromLocal8Bit(argv[1])+"/"+name+".png");qInfo()<<name<<w.size()<<"lyrics visible"<<w.findChild<lyrics_widget*>()->isVisible()<<"time"<<w.findChild<QLabel*>("timeLabel")->text()<<w.findChild<QLabel*>("timeLabel")->size();};
 shot("empty",{540,740}); const auto id=manager->get_all_playlists().first().id;manager->rename_playlist(id,"本地音乐");manager->create_new_playlist("稍后播放");QString song=QString::fromLocal8Bit(argv[2]);QStringList paths{song};QDirIterator it(QFileInfo(song).absolutePath(),{"*.mp3"},QDir::Files);while(it.hasNext()&&paths.size()<9){auto p=it.next();if(!paths.contains(p))paths<<p;}manager->add_songs_to_playlist(id,paths);
 QSignalSpy progress(controller,&playback_controller::progress_updated);QMetaObject::invokeMethod(&w,"on_tree_item_double_clicked",Q_ARG(QTreeWidgetItem*,tree->topLevelItem(0)->child(0)),Q_ARG(int,0));for(int i=0;i<30&&progress.isEmpty();++i)QTest::qWait(100);QTest::qWait(200);controller->seek(44000);QTest::qWait(300);controller->pause_resume();
 shot("wide",{860,720});
 if(auto* tabs=w.findChild<QTabBar*>("libraryTabs")){tabs->setCurrentIndex(1);shot("old-lyrics-page",{860,720});tabs->setCurrentIndex(0);}
 shot("standard-collapsed",{540,740});
 if(auto* toggle=w.findChild<QPushButton*>("lyricsToggle")){toggle->click();shot("standard-expanded",{540,740});toggle->click();}
 shot("compact-collapsed",{360,530});
 if(auto* toggle=w.findChild<QPushButton*>("lyricsToggle")){toggle->click();shot("compact-expanded",{360,530});toggle->click();}
 controller->pause_resume();shot("playing",{860,720});controller->seek(95000);shot("seek",{860,720});
 QMetaObject::invokeMethod(&w,"on_manage_playlists_action");shot("management",{800,480});QMetaObject::invokeMethod(&w,"on_manage_playlists_action");
 auto* header=tree->topLevelItem(0);tree->scrollToItem(header);QTest::qWait(80);
 QPoint branch(3,tree->visualItemRect(header).center().y());QTest::mouseClick(tree->viewport(),Qt::LeftButton,Qt::NoModifier,branch);qInfo()<<"group collapsed"<<!header->isExpanded();QTest::mouseClick(tree->viewport(),Qt::LeftButton,Qt::NoModifier,branch);qInfo()<<"group expanded"<<header->isExpanded();
 QTest::mouseClick(tree->viewport(),Qt::LeftButton,Qt::NoModifier,tree->visualItemRect(header->child(1)).center());QTest::mouseClick(tree->viewport(),Qt::LeftButton,Qt::ControlModifier,tree->visualItemRect(header->child(2)).center());qInfo()<<"multi selection"<<tree->selectedItems().count();
 QTimer::singleShot(100,&w,[]{if(auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget())){for(auto* action:menu->actions())if(action->text()=="排序列表"){action->trigger();qInfo()<<"context sort invoked";break;}menu->close();}});
 QTimer::singleShot(700,&w,[]{if(auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget()))menu->close();});
 QContextMenuEvent context(QContextMenuEvent::Mouse,tree->visualItemRect(header).center(),tree->viewport()->mapToGlobal(tree->visualItemRect(header).center()));QApplication::sendEvent(tree->viewport(),&context);
 manager->add_songs_to_playlist(id,{"/tmp/musicplayer-d/tone.wav"});auto* group=tree->topLevelItem(0);QMetaObject::invokeMethod(&w,"on_tree_item_double_clicked",Q_ARG(QTreeWidgetItem*,group->child(group->childCount()-1)),Q_ARG(int,0));shot("no-lyrics-no-cover",{860,720});
 if (qEnvironmentVariableIsSet("MUSICPLAYER_LARGE")) {
 QStringList many;QDirIterator all("/home/gyl/Music",{"*.mp3"},QDir::Files,QDirIterator::Subdirectories);while(all.hasNext()&&many.size()<1205)many<<all.next();
 manager->add_songs_to_playlist(id,many);QTest::qWait(2000);auto* group=tree->topLevelItem(0);tree->setCurrentItem(group->child(1199));w.resize(360,530);QTest::qWait(150);tree->scrollToItem(tree->currentItem(),QAbstractItemView::PositionAtCenter);shot("four-digit-index",{360,530});qInfo()<<"large count"<<group->childCount();
 }
 QMetaObject::invokeMethod(&w,"on_stop_requested");return 0;
}
