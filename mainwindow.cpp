#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QListWidget>
#include <QFileDialog>
#include <QMediaDevices>

#include "log.h"
#include "fftreal.h"
#include "mainwindow.h"
#include "spectrum_widget.h"
#include "audio_decoder_thread.h"

mainwindow::mainwindow(QWidget* parent) : QMainWindow(parent)
{
    decoder_thread_ = new audio_decoder(this);

    auto pcm_handler = [this](const uint8_t* data, size_t size, int64_t timestamp_ms)
    {
        auto packet = std::make_shared<decoded_packet>();
        packet->pcm_data.assign(data, data + size);
        packet->timestamp_ms = timestamp_ms;
        data_queue_.enqueue(packet);
    };
    decoder_thread_->set_data_callback(pcm_handler);

    setup_ui();
    setup_connections();
    init_audio_output();

    playback_timer_ = new QTimer(this);
    playback_timer_->setInterval(10);
    connect(playback_timer_, &QTimer::timeout, this, &mainwindow::playback_loop);
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
    if (item != nullptr)
    {
        start_playback(item->data(Qt::UserRole).toString());
        playlist_widget_->setCurrentItem(item);
    }
}

void mainwindow::start_playback(const QString& file_path)
{
    LOG_DEBUG("start playback");
    stop_playback();

    is_playing_ = true;
    decoder_finished_ = false;

    decoder_thread_->start_decoding(file_path);
    play_button_->setEnabled(false);
    stop_button_->setEnabled(true);

    QTimer::singleShot(200,
                       this,
                       [this]()
                       {
                           if (is_playing_)
                           {
                               playback_clock_.start();
                               playback_timer_->start();
                               spectrum_update_clock_.start();
                           }
                       });
}

void mainwindow::stop_playback()
{
    LOG_DEBUG("stop playback");
    is_playing_ = false;
    playback_timer_->stop();
    decoder_thread_->stop();
    data_queue_.clear();

    if (io_device_ != nullptr)
    {
        audio_sink_->stop();
        io_device_ = nullptr;
    }

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

void mainwindow::playback_loop()
{
    if (!is_playing_)
    {
        LOG_DEBUG("not playing");
        return;
    }

    auto packet = data_queue_.try_dequeue();
    if (packet == nullptr && decoder_finished_ && is_playing_)
    {
        LOG_DEBUG("packet queu empty, stop playback");
        stop_playback();
    }
    if (packet == nullptr)
    {
        LOG_DEBUG("not found packet");
        return;
    }

    auto elapsed = playback_clock_.elapsed();
    LOG_DEBUG("playback clock elapsed {} packet ms {}", elapsed, packet->timestamp_ms);
    if (playback_clock_.elapsed() < packet->timestamp_ms)
    {
        data_queue_.enqueue_front(packet);
        return;
    }

    if (io_device_ == nullptr)
    {
        io_device_ = audio_sink_->start();
    }
    if (io_device_ != nullptr)
    {
        io_device_->write(reinterpret_cast<const char*>(packet->pcm_data.data()), static_cast<qint64>(packet->pcm_data.size()));
    }

    update_progress(packet->timestamp_ms);
    constexpr qint64 kSpectrumUpdateIntervalMs = 150;

    if (spectrum_update_clock_.elapsed() < kSpectrumUpdateIntervalMs)
    {
        return;
    }

    spectrum_update_clock_.restart();

    const int fft_size = 128;
    const auto* pcm_data = reinterpret_cast<const qint16*>(packet->pcm_data.data());
    auto num_samples = packet->pcm_data.size() / sizeof(qint16);
    if (num_samples >= fft_size)
    {
        std::vector<double> fft_input(fft_size);
        for (int i = 0; i < fft_size; ++i)
        {
            fft_input[i] = static_cast<double>(pcm_data[i]) / 32768.0;
        }
        fft_real<double> fft(fft_size);
        fft.do_fft(fft_input.data());
        std::vector<double> magnitudes;
        magnitudes.reserve(fft_size / 2);
        for (size_t i = 1; i < fft_size / 2; ++i)
        {
            magnitudes.push_back(std::sqrt((fft.get_real(i) * fft.get_real(i)) + (fft.get_imag(i) * fft.get_imag(i))));
        }
        spectrum_widget_->update_spectrum(magnitudes);
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

QString mainwindow::format_time(qint64 time_ms)
{
    (void)this;
    int total_seconds = static_cast<int>(time_ms) / 1000;
    int minutes = total_seconds / 60;
    int seconds = total_seconds % 60;
    return QString("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
}
