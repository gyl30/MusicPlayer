#include <QColor>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QStylePainter>

#include "volumemeter.h"

volume_meter::volume_meter(QWidget* parent) : QProgressBar(parent), bar_color_(Qt::blue)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
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
    painter.setRenderHint(QPainter::Antialiasing);
    const bool horizontal = orientation() == Qt::Horizontal;
    QRectF track = horizontal ? QRectF(1, height() / 2.0 - 2, width() - 2, 4)
                              : QRectF(width() / 2.0 - 2, 1, 4, height() - 2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#D4DEEA"));
    painter.drawRoundedRect(track, 2, 2);
    const double fraction = maximum() > minimum()
                                ? qBound(0.0, double(value() - minimum()) / (maximum() - minimum()), 1.0) : 0.0;
    if (horizontal) track.setWidth(track.width() * fraction);
    else track.setTop(track.bottom() - track.height() * fraction);
    painter.setBrush(bar_color_);
    painter.drawRoundedRect(track, 2, 2);
    if (hasFocus())
    {
        painter.setPen(bar_color_);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 3, 3);
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

void volume_meter::keyPressEvent(QKeyEvent* event)
{
    int target = value();
    switch (event->key())
    {
        case Qt::Key_Right:
        case Qt::Key_Up: target += 5; break;
        case Qt::Key_Left:
        case Qt::Key_Down: target -= 5; break;
        case Qt::Key_Home: target = minimum(); break;
        case Qt::Key_End: target = maximum(); break;
        default: QProgressBar::keyPressEvent(event); return;
    }
    target = qBound(minimum(), target, maximum());
    if (target != value())
    {
        setValue(target);
        emit value_changed(target);
    }
    event->accept();
}

void volume_meter::setValueFromPosition(const QPoint& pos)
{
    double ratio = 0.0;
    if (orientation() == Qt::Horizontal)
    {
        ratio = static_cast<double>(pos.x()) / static_cast<double>(width());
    }
    else
    {
        ratio = static_cast<double>(height() - pos.y()) / static_cast<double>(height());
    }
    int newValue = static_cast<int>(ratio * maximum());
    newValue = qBound(minimum(), newValue, maximum());

    if (newValue != value())
    {
        setValue(newValue);
        emit value_changed(newValue);
    }
}
