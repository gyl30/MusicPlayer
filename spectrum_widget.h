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
    QTimer* render_timer_;
    QElapsedTimer animation_clock_;

    double dynamic_min_db_ = 100.0;
    double dynamic_max_db_ = 0.0;
    qint64 prev_timestamp_ms_ = 0;
    qint64 target_timestamp_ms_ = 0;

    std::vector<double> prev_magnitudes_;
    std::vector<double> target_magnitudes_;
    std::vector<double> display_magnitudes_;

    std::vector<std::shared_ptr<audio_packet>> packet_queue_;
};

#endif
