#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QStandardPaths>
#include "log.h"
#include "scoped_exit.h"
#include "playlist_window.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    const QString log_path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/MusicPlayer.log";
#ifdef Q_OS_WIN
    init_log(log_path.toStdWString());
#else
    init_log(QFile::encodeName(log_path).toStdString());
#endif
    DEFER(shutdown_log());

    QFile style_file(":/style/stylesheet.qss");
    if (style_file.open(QFile::ReadOnly))
    {
        QString style_sheet = QLatin1String(style_file.readAll());
        app.setStyleSheet(style_sheet);
        style_file.close();
        LOG_INFO("样式表加载成功");
    }
    else
    {
        LOG_WARN("无法加载样式表文件");
    }
    QApplication::setWindowIcon(QIcon(":/icons/app_icon.svg"));
    playlist_window main_window;
    main_window.show();
    return QApplication::exec();
}
