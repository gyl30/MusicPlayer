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
    void enterEvent(QEnterEvent* event) override;

   private:
    [[nodiscard]] bool overflows() const;
    void refresh_scroll_state();

    QTimer scroll_timer_;
    // 换文本后先停留，再滚动一圈；之后只在鼠标悬停时再滚动。
    QTimer intro_delay_;
    bool intro_pending_ = false;
    int scroll_offset_px_ = 0;
};

#endif
