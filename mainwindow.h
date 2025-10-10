#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QAudioSink>
#include <QTimer>
#include <QListWidgetItem>
#include "thread_safe_queue.h"

class QCloseEvent;
class QKeyEvent;
class QEvent;
class QSlider;
class QLabel;
class QListWidget;
class QStackedWidget;
class QVBoxLayout;
class QPushButton;
class QButtonGroup;
class QLineEdit;
class QThread;
class spectrum_widget;
class audio_decoder;

class mainwindow : public QMainWindow
{
    Q_OBJECT

   public:
    explicit mainwindow(QWidget* parent = nullptr);
    ~mainwindow() override;

   signals:
    void request_decoding(const QString& file_path, const QAudioFormat& target_format, qint64 initial_seek_ms);
    void request_stop();
    void request_seek(qint64 position_ms);

   private slots:
    void finish_playlist_edit();
    void on_list_double_clicked(QListWidgetItem* item);
    void on_playlist_context_menu_requested(const QPoint& pos);
    void on_nav_button_context_menu_requested(const QPoint& pos);
    void on_playlist_button_clicked(int id);
    void add_new_playlist();
    void delete_playlist(int index);

    void stop_playback();
    void feed_audio_device();
    void on_progress_slider_moved(int position);
    void on_seek_requested();

    void on_decoding_finished();
    void on_duration_ready(qint64 duration_ms);

    void on_seek_finished(qint64 actual_seek_ms);

   protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void setup_ui();
    void setup_connections();
    void init_audio_output();
    void create_new_playlist(const QString& name, bool is_loading = false);

    void load_playlist();
    void save_playlist();

    void update_progress(qint64 position_ms);
    [[nodiscard]] QListWidget* get_list_widget_by_index(int index) const;
    [[nodiscard]] QListWidget* current_song_list_widget() const;

   private:
    QStackedWidget* playlist_stack_ = nullptr;
    QVBoxLayout* playlist_nav_layout_ = nullptr;
    QPushButton* add_playlist_button_ = nullptr;
    QButtonGroup* playlist_button_group_ = nullptr;
    QLineEdit* currently_editing_ = nullptr;
    spectrum_widget* spectrum_widget_ = nullptr;
    QSlider* progress_slider_ = nullptr;
    QLabel* time_label_ = nullptr;

    QThread* decoder_thread_ = nullptr;
    audio_decoder* decoder_ = nullptr;

    safe_queue data_queue_;
    QAudioSink* audio_sink_ = nullptr;
    QIODevice* io_device_ = nullptr;
    QTimer* feed_timer_ = nullptr;

    bool is_playing_ = false;
    bool decoder_finished_ = false;
    qint64 total_duration_ms_ = 0;
    bool is_slider_pressed_ = false;
    QString playlist_path_;
    QString current_playing_file_path_;

    bool is_seeking_ = false;
    qint64 pending_seek_ms_ = -1;
};

#endif
