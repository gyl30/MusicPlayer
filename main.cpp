#include <QApplication>
#include <QPixmap>
#include <QPainter>
#include <QFont>
#include <QIcon>
#include "log.h"
#include "mainwindow.h"
#include "scoped_exit.h"

static QIcon emoji_to_icon(const QString &emoji, int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    auto font = QApplication::font();
    font.setPointSizeF(size * 0.8);
    painter.setFont(font);
    painter.setPen(Qt::black);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, emoji);
    painter.end();
    return pixmap;
}

int main(int argc, char* argv[])
{
    std::string app_name(argv[0]);
    init_log(app_name + ".log");
    DEFER(shutdown_log());

    QApplication app(argc, argv);
    mainwindow main_window;
    QApplication::setWindowIcon(emoji_to_icon("♫ ", 64));
    main_window.show();
    return app.exec();
}
