#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QListWidget>
#include <QFileDialog>
#include <QMediaDevices>

#include "log.h"
#include "mainwindow.h"
#include "spectrum_widget.h"
#include "audio_decoder_thread.h"

static QString format_time(qint64 time_ms)
{
    int total_seconds = static_cast<int>(time_ms) / 1000;
    int minutes = total_seconds / 60;
    int seconds = total_seconds % 60;
    return QString("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
}
mainwindow::mainwindow(QWidget* parent) : QMainWindow(parent)
{
    decoder_thread_ = new audio_decoder(this);

    auto pcm_handler = [this](const uint8_t* data, size_t size, int64_t timestamp_ms)
    {
        auto packet = std::make_shared<audio_packet>();
        packet->data.assign(data, data + size);
        packet->ms = timestamp_ms;
        data_queue_.enqueue(packet);
    };
    decoder_thread_->set_data_callback(pcm_handler);

    setup_ui();
    setup_connections();
    init_audio_output();

    setWindowTitle("音乐播放器");
    resize(800, 600);
}

mainwindow::~mainwindow() { stop_playback(); }

void mainwindow::setup_ui()
{
    auto* central_widget = new QWidget;
    auto* main_layout = new QVBoxLayout(central_widget);
    spectrum_widget_ = new spectrum_widget;
    playlist_widget_ = new QListWidget;
    auto* content_layout = new QHBoxLayout;
    content_layout->addWidget(spectrum_widget_, 2);
    content_layout->addWidget(playlist_widget_, 1);
    auto* progress_layout = new QHBoxLayout;
    progress_slider_ = new QSlider(Qt::Horizontal);
    time_label_ = new QLabel("00:00 / 00:00");
    progress_layout->addWidget(progress_slider_);
    progress_layout->addWidget(time_label_);
    auto* control_layout = new QHBoxLayout;
    open_button_ = new QPushButton("打开");
    play_button_ = new QPushButton("播放");
    stop_button_ = new QPushButton("停止");
    play_button_->setEnabled(false);
    stop_button_->setEnabled(false);
    control_layout->addStretch();
    control_layout->addWidget(open_button_);
    control_layout->addWidget(play_button_);
    control_layout->addWidget(stop_button_);
    control_layout->addStretch();
    main_layout->addLayout(content_layout);
    main_layout->addLayout(progress_layout);
    main_layout->addLayout(control_layout);
    setCentralWidget(central_widget);
}

void mainwindow::setup_connections()
{
    connect(open_button_, &QPushButton::clicked, this, &mainwindow::on_open_file);
    connect(playlist_widget_, &QListWidget::itemDoubleClicked, this, &mainwindow::on_list_double_clicked);
    connect(play_button_,
            &QPushButton::clicked,
            [this]()
            {
                if (playlist_widget_->currentItem())
                {
                    on_list_double_clicked(playlist_widget_->currentItem());
                }
            });
    connect(stop_button_, &QPushButton::clicked, this, &mainwindow::stop_playback);
    connect(decoder_thread_, &audio_decoder::decoding_finished, this, &mainwindow::on_decoding_finished, Qt::QueuedConnection);
    connect(decoder_thread_, &audio_decoder::duration_ready, this, &mainwindow::on_duration_ready, Qt::QueuedConnection);
    connect(progress_slider_, &QSlider::sliderMoved, this, &mainwindow::on_slider_moved);
}

void mainwindow::init_audio_output()
{
    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);
    audio_sink_ = new QAudioSink(QMediaDevices::defaultAudioOutput(), format);
    audio_sink_->setBufferSize(44100L * 2 * 2 * 2);
}

void mainwindow::on_open_file()
{
    QStringList file_paths = QFileDialog::getOpenFileNames(this, "选择音乐文件", "", "音频文件 (*.mp3 *.flac *.ogg *.wav)");
    for (const QString& path : file_paths)
    {
        auto* item = new QListWidgetItem(QFileInfo(path).fileName());
        item->setData(Qt::UserRole, path);
        playlist_widget_->addItem(item);
    }
    if (playlist_widget_->count() > 0 && !is_playing_)
    {
        play_button_->setEnabled(true);
    }
}

void mainwindow::on_list_double_clicked(QListWidgetItem* item)
{
    if (item == nullptr)
    {
        return;
    }

    LOG_DEBUG("start playback");
    stop_playback();

    io_device_ = audio_sink_->start();
    if (io_device_ == nullptr)
    {
        LOG_ERROR("启动音频设备的IODevice失败。");
        return;
    }
    is_playing_ = true;
    decoder_finished_ = false;

    spectrum_widget_->start_playback();
    decoder_thread_->start_decoding(item->data(Qt::UserRole).toString());
    playlist_widget_->setCurrentItem(item);

    play_button_->setEnabled(false);
    stop_button_->setEnabled(true);

    QTimer::singleShot(50, this, &mainwindow::feed_audio_device);
}

void mainwindow::stop_playback()
{
    LOG_DEBUG("stop playback");
    is_playing_ = false;
    decoder_thread_->stop();
    data_queue_.clear();
    spectrum_widget_->stop_playback();

    if (audio_sink_ != nullptr)
    {
        audio_sink_->stop();
    }
    io_device_ = nullptr;

    update_progress(0);
    stop_button_->setEnabled(false);
    play_button_->setEnabled(playlist_widget_->count() > 0);
}

void mainwindow::on_decoding_finished()
{
    LOG_DEBUG("decoding finished");
    decoder_finished_ = true;
}
void mainwindow::on_duration_ready(qint64 duration_ms)
{
    LOG_DEBUG("duration ready {}", duration_ms);
    total_duration_ms_ = duration_ms;
    progress_slider_->setRange(0, static_cast<int>(total_duration_ms_));
    update_progress(0);
}

void mainwindow::feed_audio_device()
{
    if (!is_playing_)
    {
        return;
    }

    std::shared_ptr<audio_packet> last_packet = nullptr;
    while (audio_sink_->bytesFree() > 0 && !data_queue_.is_empty())
    {
        auto packet = data_queue_.try_dequeue();
        if (packet == nullptr)
        {
            break;
        }

        io_device_->write(reinterpret_cast<const char*>(packet->data.data()), static_cast<qint64>(packet->data.size()));
        spectrum_widget_->enqueue_packet(packet);
        last_packet = packet;
    }

    if (data_queue_.is_empty() && decoder_finished_)
    {
        const int bytes_per_second = 44100 * 2 * 2;
        const qint64 bytes_buffered = audio_sink_->bufferSize() - audio_sink_->bytesFree();
        if (bytes_buffered > 0)
        {
            const qint64 buffered_duration_ms = (bytes_buffered * 1000) / bytes_per_second;
            QTimer::singleShot(buffered_duration_ms + 100, this, &mainwindow::stop_playback);
        }
        else
        {
            stop_playback();
        }
        return;
    }
    if (last_packet != nullptr)
    {
        update_progress(last_packet->ms);
    }
    if (is_playing_)
    {
        const int bytes_per_second = 44100 * 2 * 2;
        const qint64 bytes_buffered = audio_sink_->bufferSize() - audio_sink_->bytesFree();
        const qint64 buffered_duration_ms = (bytes_buffered * 1000) / bytes_per_second;

        auto next_delay_ms = buffered_duration_ms / 2;
        next_delay_ms = qBound(10, next_delay_ms, 1000);

        QTimer::singleShot(next_delay_ms, this, &mainwindow::feed_audio_device);
    }
}

void mainwindow::on_slider_moved(int position) { update_progress(position); }

void mainwindow::update_progress(qint64 position_ms)
{
    LOG_DEBUG("update progress {}", position_ms);
    if (!progress_slider_->isSliderDown())
    {
        progress_slider_->setValue(static_cast<int>(position_ms));
    }
    time_label_->setText(QString("%1 / %2").arg(format_time(position_ms)).arg(format_time(total_duration_ms_)));
}
