#ifndef LYRICS_WIDGET_H
#define LYRICS_WIDGET_H

#include <QWidget>
#include <QList>
#include <QTimer>
#include <QFont>
#include <QColor>
#include <QRectF>
#include "audio_packet.h"

class lyrics_widget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QColor normalColor READ normal_color WRITE set_normal_color)
    Q_PROPERTY(QColor activeColor READ active_color WRITE set_active_color)

   public:
    explicit lyrics_widget(QWidget* parent = nullptr);
    ~lyrics_widget() override;

    void set_lyrics(const QList<LyricLine>& lyrics);
    void set_current_time(qint64 time_ms);
    void clear();
    [[nodiscard]] bool has_lyrics() const { return !lyrics_.isEmpty(); }
    [[nodiscard]] QColor normal_color() const { return color_normal_; }
    void set_normal_color(const QColor& color)
    {
        color_normal_ = color;
        update();
    }
    [[nodiscard]] QColor active_color() const { return color_active_; }
    void set_active_color(const QColor& color)
    {
        color_active_ = color;
        update();
    }

   protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

   private slots:
    void on_animation_timer();

   private:
    void update_layout();
    [[nodiscard]] int get_line_at_time(qint64 time_ms) const;

   private:
    QList<LyricLine> lyrics_;
    QList<QRectF> line_rects_;

    int current_index_ = -1;

    double current_scroll_y_ = 0.0;
    double target_scroll_y_ = 0.0;
    QTimer* animation_timer_ = nullptr;

    QFont font_normal_;
    QFont font_active_;
    QColor color_normal_;
    QColor color_active_;
    int line_spacing_ = 15;
    int vertical_padding_ = 50;

    bool layout_dirty_ = false;
};

#endif
