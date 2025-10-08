#ifndef SPECTRUM_WIDGET_H
#define SPECTRUM_WIDGET_H

#include <QWidget>
#include <vector>
#include <memory>
#include <QTimer>
#include <QElapsedTimer>
#include <QMutex>
#include "audio_packet.h"

class spectrum_widget : public QWidget
{
    Q_OBJECT

   public:
    explicit spectrum_widget(QWidget* parent = nullptr);
    ~spectrum_widget() override = default;

   public:
    void enqueue_packet(const std::shared_ptr<audio_packet>& packet);
    void start_playback();
    void stop_playback();

   protected:
    void paintEvent(QPaintEvent* event) override;

   private slots:
    void on_render_timeout();

   private:
    qint64 prev_timestamp_ms_ = 0;
    qint64 target_timestamp_ms_ = 0;
    QTimer* render_timer_;
    QElapsedTimer animation_clock_;
    std::vector<double> prev_magnitudes_;
    std::vector<double> target_magnitudes_;
    std::vector<double> display_magnitudes_;
    std::vector<std::shared_ptr<audio_packet>> packet_queue_;
    double min_rendered_db_ = 0.0;
    double max_rendered_db_ = 0.0;
};

#endif
