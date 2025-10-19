#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include "log.h"
#include "mainwindow.h"
#include "scoped_exit.h"

static QIcon create_text_icon(const QString& text, int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    auto font = QApplication::font();
    font.setPointSizeF(size * 0.7);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, text);
    painter.end();
    return QIcon{pixmap};
}

int main(int argc, char* argv[])
{
    std::string app_name(argv[0]);
    init_log(app_name + ".log");
    DEFER(shutdown_log());

    QApplication app(argc, argv);

    mainwindow main_window;
    main_window.setWindowIcon(create_text_icon("♫", 64));
    main_window.show();
    return QApplication::exec();
}
