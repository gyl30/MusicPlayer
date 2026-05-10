#ifndef DIGITAL_TIME_LABEL_H
#define DIGITAL_TIME_LABEL_H

#include <QLabel>

class digital_time_label : public QLabel
{
   public:
    explicit digital_time_label(QWidget* parent = nullptr);

    QSize sizeHint() const override;

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    [[nodiscard]] int text_width(const QString& text, int digit_height) const;
    void draw_digit(QPainter& painter, int x, int y, int width, int height, QChar digit) const;
    void draw_colon(QPainter& painter, int x, int y, int width, int height) const;
};

#endif
