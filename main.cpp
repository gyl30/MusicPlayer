#include <QApplication>
#include <QFile>
#include <QIcon>
#include "log.h"
#include "mainwindow.h"
#include "scoped_exit.h"

int main(int argc, char* argv[])
{
    std::string app_name(argv[0]);
    init_log(app_name + ".log");
    DEFER(shutdown_log());

    QApplication app(argc, argv);

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

    mainwindow main_window;
    main_window.setWindowIcon(QIcon(":/icons/app_icon.svg"));
    main_window.show();
    return QApplication::exec();
}
