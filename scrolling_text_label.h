#ifndef SCROLLING_TEXT_LABEL_H
#define SCROLLING_TEXT_LABEL_H

#include <QLabel>
#include <QTimer>

class scrolling_text_label : public QLabel
{
   public:
    explicit scrolling_text_label(QWidget* parent = nullptr);

    void setText(const QString& text);
    QSize sizeHint() const override;

   protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

   private:
    void refresh_scroll_state();

    QTimer scroll_timer_;
    int scroll_offset_px_ = 0;
};

#endif
