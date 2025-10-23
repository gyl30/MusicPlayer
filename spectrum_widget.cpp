#include <cmath>
#include <QThread>
#include <QPainter>
#include <algorithm>
#include <QPaintEvent>
#include <QMetaObject>

#include "spectrum_widget.h"
#include "spectrum_processor.h"

constexpr int kNumBars = 128;
constexpr double kMinDbRange = 20.0;
constexpr double kMaxDbDecayPerSecond = 5.0;
constexpr double kMinDbRisePerSecond = 4.0;

constexpr double kBarRiseFactor = 0.6;
constexpr double kBarFallFactor = 0.25;

spectrum_widget::spectrum_widget(QWidget* parent) : QWidget(parent)
{
    qRegisterMetaType<std::shared_ptr<audio_packet>>("std::shared_ptr<audio_packet>");
    qRegisterMetaType<std::vector<double>>("std::vector<double>");
    setAutoFillBackground(false);

    spectrum_thread_ = new QThread(this);
    processor_ = new spectrum_processor();
    processor_->moveToThread(spectrum_thread_);

    connect(processor_, &spectrum_processor::magnitudes_ready, this, &spectrum_widget::update_display, Qt::QueuedConnection);
    connect(spectrum_thread_, &QThread::finished, processor_, &QObject::deleteLater);

    spectrum_thread_->start();
    frame_timer_.start();
}

spectrum_widget::~spectrum_widget()
{
    spectrum_thread_->quit();
    spectrum_thread_->wait();
}

void spectrum_widget::enqueue_packet(const std::shared_ptr<audio_packet>& packet)
{
    QMetaObject::invokeMethod(processor_, "process_packet", Qt::QueuedConnection, Q_ARG(std::shared_ptr<audio_packet>, packet));
}

void spectrum_widget::reset_and_start(qint64 session_id, qint64 start_offset_ms)
{
    session_id_ = session_id;
    dynamic_min_db_ = 100.0;
    dynamic_max_db_ = 0.0;
    frame_timer_.restart();
    last_paint_time_ms_ = 0;

    smoothed_bar_heights_.clear();

    QMetaObject::invokeMethod(processor_, "reset_and_start", Qt::QueuedConnection, Q_ARG(qint64, start_offset_ms));
    emit playback_started(session_id_);
}

void spectrum_widget::stop_playback() { QMetaObject::invokeMethod(processor_, "stop_playback", Qt::QueuedConnection); }

void spectrum_widget::update_display(const std::vector<double>& magnitudes)
{
    display_magnitudes_ = magnitudes;
    update();
}

void spectrum_widget::paintEvent(QPaintEvent* /*event*/)
{
    qint64 current_time_ms = frame_timer_.elapsed();
    if (last_paint_time_ms_ == 0)
    {
        last_paint_time_ms_ = current_time_ms;
    }
    double delta_time_s = static_cast<double>(current_time_ms - last_paint_time_ms_) / 1000.0;
    last_paint_time_ms_ = current_time_ms;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (display_magnitudes_.empty())
    {
        return;
    }

    std::vector<double> target_db_values;
    target_db_values.reserve(kNumBars);
    auto data_points_per_bar = display_magnitudes_.size() / kNumBars;
    if (data_points_per_bar == 0)
    {
        data_points_per_bar = 1;
    }

    for (size_t i = 0; i < kNumBars; ++i)
    {
        double avg_magnitude = 0;
        int count = 0;
        for (size_t j = i * data_points_per_bar; j < (i + 1) * data_points_per_bar && j < display_magnitudes_.size(); ++j)
        {
            avg_magnitude += display_magnitudes_[j];
            count++;
        }
        if (count > 0)
        {
            avg_magnitude /= count;
        }
        double db_value = 20 * log10(avg_magnitude + 1e-9);
        target_db_values.push_back(qMax(0.0, db_value));
    }

    double current_frame_min_db = 1000.0;
    double current_frame_max_db = 0.0;
    for (double db : target_db_values)
    {
        current_frame_min_db = std::min(db, current_frame_min_db);
        current_frame_max_db = std::max(db, current_frame_max_db);
    }
    const double max_db_decay_this_frame = kMaxDbDecayPerSecond * delta_time_s;
    const double min_db_rise_this_frame = kMinDbRisePerSecond * delta_time_s;
    dynamic_max_db_ =
        (current_frame_max_db > dynamic_max_db_) ? current_frame_max_db : std::max(dynamic_max_db_ - max_db_decay_this_frame, current_frame_max_db);
    dynamic_min_db_ =
        (current_frame_min_db < dynamic_min_db_) ? current_frame_min_db : std::min(dynamic_min_db_ + min_db_rise_this_frame, current_frame_min_db);
    double range = std::max(dynamic_max_db_ - dynamic_min_db_, kMinDbRange);

    if (smoothed_bar_heights_.size() != kNumBars)
    {
        smoothed_bar_heights_.assign(kNumBars, 0.0);
    }

    for (size_t i = 0; i < kNumBars; ++i)
    {
        double target_height_ratio = (target_db_values[i] - dynamic_min_db_) / range;
        target_height_ratio = qBound(0.0, target_height_ratio, 1.0);

        double current_height_ratio = smoothed_bar_heights_[i];

        double factor = (target_height_ratio > current_height_ratio) ? kBarRiseFactor : kBarFallFactor;

        smoothed_bar_heights_[i] += (target_height_ratio - current_height_ratio) * factor;
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(173, 216, 230));
    double bar_width = static_cast<double>(width()) / kNumBars;

    for (size_t i = 0; i < kNumBars; ++i)
    {
        double bar_height = smoothed_bar_heights_[i] * height();

        if (bar_height > 0)
        {
            double x = static_cast<double>(i) * bar_width;
            QRectF bar_rect(x, height() - bar_height, bar_width, bar_height);
            painter.drawRect(bar_rect);
        }
    }
}
