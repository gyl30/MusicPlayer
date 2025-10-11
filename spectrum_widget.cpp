#include <QPainter>
#include <QPaintEvent>
#include <QThread>
#include <QMetaObject>
#include <algorithm>
#include <cmath>
#include "spectrum_widget.h"
#include "spectrum_processor.h"

constexpr int kNumBars = 25;
constexpr int kBlockHeight = 15;
constexpr int kBlockSpacing = 4;
constexpr double kMinDbRange = 20.0;
constexpr double kMaxDbDecayRate = 0.3;
constexpr double kMinDbRiseRate = 0.2;
constexpr double kIdleBarHeight = 2.0;

spectrum_widget::spectrum_widget(QWidget* parent) : QWidget(parent)
{
    qRegisterMetaType<std::shared_ptr<audio_packet>>("std::shared_ptr<audio_packet>");
    qRegisterMetaType<std::vector<double>>("std::vector<double>");

    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    setMinimumHeight(100);

    spectrum_thread_ = new QThread(this);
    processor_ = new spectrum_processor();
    processor_->moveToThread(spectrum_thread_);

    connect(processor_, &spectrum_processor::magnitudes_ready, this, &spectrum_widget::update_display, Qt::QueuedConnection);
    connect(spectrum_thread_, &QThread::finished, processor_, &QObject::deleteLater);

    spectrum_thread_->start();
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

void spectrum_widget::start_playback(qint64 start_offset_ms)
{
    dynamic_min_db_ = 100.0;
    dynamic_max_db_ = 0.0;

    QMetaObject::invokeMethod(processor_, "start_playback", Qt::QueuedConnection, Q_ARG(qint64, start_offset_ms));
}

void spectrum_widget::stop_playback() { QMetaObject::invokeMethod(processor_, "stop_playback", Qt::QueuedConnection); }

void spectrum_widget::update_display(const std::vector<double>& magnitudes)
{
    display_magnitudes_ = magnitudes;
    update();
}

void spectrum_widget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.setPen(Qt::NoPen);

    if (display_magnitudes_.empty())
    {
        painter.setBrush(QColor(60, 60, 60));
        double total_bar_width = static_cast<double>(width()) / kNumBars;
        double bar_draw_width = total_bar_width * 0.8;
        for (int i = 0; i < kNumBars; ++i)
        {
            double bar_x_position = (i * total_bar_width) + (total_bar_width * 0.1);
            QRectF block_rect(bar_x_position, height() - kIdleBarHeight, bar_draw_width, kIdleBarHeight);
            painter.drawRect(block_rect);
        }
        return;
    }

    std::vector<double> current_frame_db_values;
    current_frame_db_values.reserve(kNumBars);
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
        current_frame_db_values.push_back(qMax(0.0, db_value));
    }

    double current_frame_min_db = 1000.0;
    double current_frame_max_db = 0.0;
    for (double db : current_frame_db_values)
    {
        current_frame_min_db = std::min(db, current_frame_min_db);
        current_frame_max_db = std::max(db, current_frame_max_db);
    }
    if (current_frame_max_db > dynamic_max_db_)
    {
        dynamic_max_db_ = current_frame_max_db;
    }
    else
    {
        dynamic_max_db_ -= kMaxDbDecayRate;
        dynamic_max_db_ = std::max(dynamic_max_db_, current_frame_max_db);
    }
    if (current_frame_min_db < dynamic_min_db_)
    {
        dynamic_min_db_ = current_frame_min_db;
    }
    else
    {
        dynamic_min_db_ += kMinDbRiseRate;
        dynamic_min_db_ = std::min(dynamic_min_db_, current_frame_min_db);
    }

    double range = dynamic_max_db_ - dynamic_min_db_;
    range = std::max(range, kMinDbRange);

    QLinearGradient gradient(0, 0, 0, height());
    gradient.setColorAt(0.0, Qt::red);
    gradient.setColorAt(0.45, Qt::yellow);
    gradient.setColorAt(1.0, Qt::green);

    QBrush idle_brush(QColor(60, 60, 60));

    double total_bar_width = static_cast<double>(width()) / kNumBars;
    double bar_draw_width = total_bar_width * 0.8;

    for (int i = 0; i < kNumBars; ++i)
    {
        double db_value = current_frame_db_values[static_cast<size_t>(i)];
        double height_ratio = (db_value - dynamic_min_db_) / range;
        double bar_height = qBound(0.0, height_ratio, 1.0) * height();

        double bar_x_position = (i * total_bar_width) + (total_bar_width * 0.1);

        if (bar_height < kIdleBarHeight)
        {
            bar_height = kIdleBarHeight;
            painter.setBrush(idle_brush);
        }
        else
        {
            painter.setBrush(gradient);
        }

        double height_to_draw = bar_height;
        double current_block_bottom_y = height();
        while (height_to_draw > 0)
        {
            double current_block_h = std::min(height_to_draw, static_cast<double>(kBlockHeight));
            double block_top_y = current_block_bottom_y - current_block_h;
            QRectF block_rect(bar_x_position, block_top_y, bar_draw_width, current_block_h);
            painter.drawRect(block_rect);
            height_to_draw -= (kBlockHeight + kBlockSpacing);
            current_block_bottom_y -= (kBlockHeight + kBlockSpacing);
        }
    }
}
