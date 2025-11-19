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

   public:
    explicit lyrics_widget(QWidget* parent = nullptr);
    ~lyrics_widget() override;

    void set_lyrics(const QList<LyricLine>& lyrics);
    void set_current_time(qint64 time_ms);
    void clear();

   protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

   private slots:
    void on_animation_timer();

   private:
    void update_layout();
    [[nodiscard]] int get_line_at_time(qint64 time_ms) const;

   private:
    QList<LyricLine> lyrics_;
    QList<QRectF> line_rects_;

    qint64 current_time_ms_ = 0;
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
