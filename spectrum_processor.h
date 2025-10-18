#ifndef SPECTRUM_PROCESSOR_H
#define SPECTRUM_PROCESSOR_H

#include <vector>
#include <memory>
#include <deque>
#include <QTimer>
#include <QObject>
#include <QElapsedTimer>

#include "audio_packet.h"

class spectrum_processor : public QObject
{
    Q_OBJECT

   public:
    explicit spectrum_processor(QObject* parent = nullptr);
    ~spectrum_processor() override = default;

   signals:
    void magnitudes_ready(const std::vector<double>& magnitudes);

   public slots:
    void reset_and_start(qint64 start_offset_ms);
    void stop_playback();
    void process_packet(const std::shared_ptr<audio_packet>& packet);

   private slots:
    void on_render_timeout();

   private:
    QTimer* render_timer_;
    QElapsedTimer animation_clock_;

    qint64 prev_timestamp_ms_ = 0;
    qint64 target_timestamp_ms_ = 0;
    qint64 start_offset_ms_ = 0;

    std::vector<double> prev_magnitudes_;
    std::vector<double> target_magnitudes_;

    std::deque<std::shared_ptr<audio_packet>> packet_queue_;
    bool needs_resync_ = false;
};

#endif
