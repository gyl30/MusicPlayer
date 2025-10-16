#ifndef SPECTRUM_WIDGET_H
#define SPECTRUM_WIDGET_H

#include <vector>
#include <memory>
#include <QWidget>
#include "audio_packet.h"

class QThread;
class spectrum_processor;

class spectrum_widget : public QWidget
{
    Q_OBJECT

   public:
    explicit spectrum_widget(QWidget* parent = nullptr);
    ~spectrum_widget() override;

   public slots:
    void enqueue_packet(const std::shared_ptr<audio_packet>& packet);
    void start_playback(qint64 session_id, qint64 start_offset_ms = 0);
    void stop_playback();

   signals:
    void playback_started(qint64 session_id);

   protected:
    void paintEvent(QPaintEvent* event) override;

   private slots:
    void update_display(const std::vector<double>& magnitudes);

   private:
    QThread* spectrum_thread_;
    spectrum_processor* processor_;
    qint64 session_id_ = 0;
    double dynamic_min_db_ = 100.0;
    double dynamic_max_db_ = 0.0;
    std::vector<double> display_magnitudes_;
};

#endif
