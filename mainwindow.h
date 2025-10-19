#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>

class volume_meter;
class QCloseEvent;
class QSlider;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class spectrum_widget;
class playback_controller;
class playlist_manager;

class mainwindow : public QMainWindow
{
    Q_OBJECT

   public:
    explicit mainwindow(QWidget* parent = nullptr);
    ~mainwindow() override;

   private slots:
    void on_tree_item_double_clicked(QTreeWidgetItem* item, int column);
    void on_seek_requested();
    void on_play_pause_clicked();
    void on_next_clicked();
    void on_prev_clicked();
    void on_stop_clicked();
    void on_volume_changed(int value);

    void update_track_info(qint64 duration_ms);
    void on_playback_started(const QString& file_path, const QString& file_name);
    void update_progress(qint64 current_ms, qint64 total_ms);
    void handle_playback_finished();
    void handle_playback_error(const QString& error_message);

    void rebuild_ui_from_playlists();

   protected:
    void closeEvent(QCloseEvent* event) override;

   private:
    void setup_ui();
    void setup_connections();
    void clear_playing_indicator();

   private:
    playback_controller* controller_ = nullptr;
    playlist_manager* playlist_manager_ = nullptr;

    QTreeWidget* song_tree_widget_ = nullptr;
    spectrum_widget* spectrum_widget_ = nullptr;

    QSlider* progress_slider_ = nullptr;
    volume_meter* volume_meter_ = nullptr;

    QPushButton* prev_button_ = nullptr;
    QPushButton* play_pause_button_ = nullptr;
    QPushButton* next_button_ = nullptr;
    QPushButton* stop_button_ = nullptr;

    QLabel* song_title_label_ = nullptr;
    QLabel* time_label_ = nullptr;

    QTreeWidgetItem* currently_playing_item_ = nullptr;
    QString current_playing_file_path_;
    bool is_playing_ = false;
    bool is_paused_ = false;
    bool is_slider_pressed_ = false;
};
#endif
