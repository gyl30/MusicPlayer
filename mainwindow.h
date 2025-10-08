#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QAudioSink>
#include <QTimer>
#include <QElapsedTimer>
#include <QListWidgetItem>
#include "thread_safe_queue.h"

class QCloseEvent;
class QKeyEvent;
class QSlider;
class QLabel;
class QListWidget;
class QTabWidget;
class spectrum_widget;
class audio_decoder;

class mainwindow : public QMainWindow
{
    Q_OBJECT

   public:
    explicit mainwindow(QWidget* parent = nullptr);
    ~mainwindow() override;

   private slots:
    void on_list_double_clicked(QListWidgetItem* item);
    void on_playlist_context_menu_requested(const QPoint& pos);
    void on_tab_bar_context_menu_requested(const QPoint& pos);
    void add_new_playlist();
    void delete_playlist(int index);

    void stop_playback();
    void feed_audio_device();
    void on_slider_moved(int position);

    void on_decoding_finished();
    void on_duration_ready(qint64 duration_ms);

   protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

   private:
    void setup_ui();
    void setup_connections();
    void init_audio_output();
    void create_new_playlist_tab(const QString& name);

    void load_playlist();
    void save_playlist();

    void update_progress(qint64 position_ms);
    [[nodiscard]] QListWidget* current_playlist_widget() const;

   private:
    QSlider* progress_slider_ = nullptr;
    QLabel* time_label_ = nullptr;
    QTabWidget* playlist_tabs_ = nullptr;
    spectrum_widget* spectrum_widget_ = nullptr;

    audio_decoder* decoder_thread_ = nullptr;
    safe_queue data_queue_;
    QAudioSink* audio_sink_ = nullptr;
    QIODevice* io_device_ = nullptr;

    bool is_playing_ = false;
    bool decoder_finished_ = false;
    qint64 total_duration_ms_ = 0;

    QString playlist_path_;
};

#endif
