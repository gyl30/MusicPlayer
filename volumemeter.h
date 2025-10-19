#ifndef VOLUMEMETER_H
#define VOLUMEMETER_H

#include <QProgressBar>

class volume_meter : public QProgressBar
{
    Q_OBJECT

   public:
    explicit volume_meter(QWidget* parent = nullptr);

   signals:
    void value_changed(int value);

   protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

   private:
    void set_value_from_position(const QPoint& pos);
};

#endif
