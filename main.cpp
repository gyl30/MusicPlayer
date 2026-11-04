#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QStandardPaths>
#include <QSysInfo>
#include "log.h"
#include "scoped_exit.h"
#include "playlist_window.h"

extern "C"
{
#include <libavutil/avutil.h>
}

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
    LOG_INFO("程序启动 qt {} ffmpeg {} os {} log {}",
             qVersion(),
             av_version_info(),
             QSysInfo::prettyProductName().toStdString(),
             log_path.toStdString());

    QApplication::setWindowIcon(QIcon(":/icons/app_icon.svg"));
    playlist_window main_window;
    main_window.show();
    // 日志末尾没有这一行说明程序是崩溃或被强制结束的。
    const int exit_code = QApplication::exec();
    LOG_INFO("程序退出 code {}", exit_code);
    return exit_code;
}
