#include <QColor>
#include <QMouseEvent>
#include <QStylePainter>
#include <QStyleOptionProgressBar>

#include "volumemeter.h"

volume_meter::volume_meter(QWidget* parent) : QProgressBar(parent), bar_color_(Qt::blue)
{
    setMouseTracking(true);
    setAutoFillBackground(false);
}

void volume_meter::mousePressEvent(QMouseEvent* event) { setValueFromPosition(event->pos()); }

void volume_meter::mouseMoveEvent(QMouseEvent* event)
{
    if ((event->buttons() & Qt::LeftButton) != 0U)
    {
        setValueFromPosition(event->pos());
    }
}

void volume_meter::paintEvent(QPaintEvent* /*event*/)
{
    QStylePainter painter(this);
    QStyleOptionProgressBar option;
    initStyleOption(&option);

    const int numBlocks = 10;
    double blockHeight = static_cast<double>(height()) / numBlocks;
    int litBlocks = static_cast<int>((static_cast<double>(value()) / maximum()) * numBlocks);

    for (int i = 0; i < litBlocks; ++i)
    {
        double y = height() - ((i + 1) * blockHeight);
        QRectF blockRect(0, y, width(), blockHeight);
        painter.fillRect(blockRect.adjusted(1, 1, -1, -1), bar_color_);
    }
}

void volume_meter::wheelEvent(QWheelEvent* event)
{
    const int singleStep = 5;
    int currentValue = value();
    int newValue = currentValue;

    if (event->angleDelta().y() > 0)
    {
        newValue += singleStep;
    }
    else if (event->angleDelta().y() < 0)
    {
        newValue -= singleStep;
    }

    newValue = qBound(minimum(), newValue, maximum());

    if (newValue != currentValue)
    {
        setValue(newValue);
        emit value_changed(newValue);
    }

    event->accept();
}

void volume_meter::setValueFromPosition(const QPoint& pos)
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
