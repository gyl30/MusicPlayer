#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QAudioSink>
#include <QTimer>
#include <QElapsedTimer>
#include <QListWidgetItem>
#include "thread_safe_queue.h"

class QPushButton;
class QSlider;
class QLabel;
class QListWidget;
class spectrum_widget;
class audio_decoder;

class mainwindow : public QMainWindow
{
    Q_OBJECT

   public:
    explicit mainwindow(QWidget* parent = nullptr);
    ~mainwindow();

   private slots:
    void on_open_file();
    void on_list_double_clicked(QListWidgetItem* item);
    void start_playback(const QString& file_path);
    void stop_playback();

    void playback_loop();

    void on_decoding_finished();
    void on_duration_ready(qint64 duration_ms);

    void on_slider_moved(int position);

   private:
    void setup_ui();
    void setup_connections();
    void init_audio_output();
    void update_progress(qint64 position_ms);
    QString format_time(qint64 time_ms);

    QPushButton* play_button_ = nullptr;
    QPushButton* stop_button_ = nullptr;
    QPushButton* open_button_ = nullptr;
    QSlider* progress_slider_ = nullptr;
    QLabel* time_label_ = nullptr;
    QListWidget* playlist_widget_ = nullptr;
    spectrum_widget* spectrum_widget_ = nullptr;

    audio_decoder* decoder_thread_ = nullptr;
    safe_queue data_queue_;
    QAudioSink* audio_sink_ = nullptr;
    QIODevice* io_device_ = nullptr;
    QTimer* playback_timer_ = nullptr;
    QElapsedTimer playback_clock_;

    bool is_playing_ = false;
    bool decoder_finished_ = false;
    qint64 total_duration_ms_ = 0;
};

#endif    // MAIN_WINDOW_H
