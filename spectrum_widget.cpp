#include <QPainter>
#include <QPaintEvent>
#include <QLinearGradient>
#include <algorithm>
#include <cmath>
#include "spectrum_widget.h"

spectrum_widget::spectrum_widget(QWidget* parent) : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    setMinimumHeight(100);
}

void spectrum_widget::update_spectrum(const std::vector<double>& magnitudes)
{
    magnitudes_ = magnitudes;
    update();
}

void spectrum_widget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    QPainter painter(this);

    if (magnitudes_.empty())
    {
        return;
    }

    int num_bars = qMin(static_cast<int>(magnitudes_.size()), 256);
    double bar_width = static_cast<double>(width()) / num_bars;
    double max_magnitude = 50.0;

    for (int i = 0; i < num_bars; ++i)
    {
        double db_value = 20 * log10(magnitudes_[i] + 1e-9);
        db_value = qMax(0.0, db_value);

        double bar_height_ratio = db_value / max_magnitude;
        bar_height_ratio = std::min(bar_height_ratio, 1.0);

        double bar_height = bar_height_ratio * height();
        QRectF bar(i * bar_width, height() - bar_height, bar_width , bar_height);

        QLinearGradient gradient(bar.topLeft(), bar.bottomLeft());
        gradient.setColorAt(1.0, Qt::green);
        gradient.setColorAt(0.5, Qt::yellow);
        gradient.setColorAt(0.0, Qt::red);

        painter.fillRect(bar, gradient);
    }
}
