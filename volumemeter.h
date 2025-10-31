#ifndef VOLUMEMETER_H
#define VOLUMEMETER_H

#include <QProgressBar>
#include <QWheelEvent>
#include <QColor>

class volume_meter : public QProgressBar
{
    Q_OBJECT
    Q_PROPERTY(QColor barColor READ getBarColor WRITE setBarColor)

   public:
    explicit volume_meter(QWidget* parent = nullptr);

    [[nodiscard]] QColor getBarColor() const { return bar_color_; }
    void setBarColor(const QColor& color)
    {
        bar_color_ = color;
        update();
    }

   signals:
    void value_changed(int value);

   protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

   private:
    void setValueFromPosition(const QPoint& pos);
    QColor bar_color_;
};

#endif
