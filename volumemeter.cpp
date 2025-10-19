#include <QColor>
#include <QMouseEvent>
#include <QStylePainter>
#include <QStyleOptionProgressBar>

#include "volumemeter.h"

volume_meter::volume_meter(QWidget* parent) : QProgressBar(parent) { setMouseTracking(true); }

void volume_meter::mousePressEvent(QMouseEvent* event) { set_value_from_position(event->pos()); }

void volume_meter::mouseMoveEvent(QMouseEvent* event)
{
    if ((event->buttons() & Qt::LeftButton) != 0U)
    {
        set_value_from_position(event->pos());
    }
}

void volume_meter::paintEvent(QPaintEvent* /*event*/)
{
    QStylePainter painter(this);
    QStyleOptionProgressBar option;
    initStyleOption(&option);

    painter.drawControl(QStyle::CE_ProgressBarGroove, option);

    const int numBlocks = 10;
    double blockHeight = static_cast<double>(height()) / numBlocks;
    int litBlocks = static_cast<int>((static_cast<double>(value()) / maximum()) * numBlocks);

    for (int i = 0; i < litBlocks; ++i)
    {
        double y = height() - ((i + 1) * blockHeight);
        QRectF blockRect(0, y, width(), blockHeight);
        painter.fillRect(blockRect.adjusted(2, 2, -2, -2), option.palette.color(QPalette::Highlight));
    }
}

void volume_meter::set_value_from_position(const QPoint& pos)
{
    double ratio = static_cast<double>(height() - pos.y()) / static_cast<double>(height());
    int newValue = static_cast<int>(ratio * maximum());
    newValue = qBound(minimum(), newValue, maximum());

    if (newValue != value())
    {
        setValue(newValue);
        emit value_changed(newValue);
    }
}
