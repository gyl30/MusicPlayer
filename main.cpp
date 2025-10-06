#include <QApplication>
#include "log.h"
#include "mainwindow.h"
#include "scoped_exit.h"

int main(int argc, char* argv[])
{
    std::string app_name(argv[0]);
    init_log(app_name + ".log");
    set_level("debug");
    DEFER(shutdown_log());

    QApplication app(argc, argv);
    mainwindow main_window;
    main_window.show();
    return app.exec();
}
