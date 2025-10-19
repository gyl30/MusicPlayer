#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStackedLayout>
#include <QGridLayout>
#include <QLabel>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QStyle>
#include <QTime>

#include "mainwindow.h"
#include "volumemeter.h"
#include "spectrum_widget.h"
#include "playlist_manager.h"
#include "playback_controller.h"

mainwindow::mainwindow(QWidget* parent) : QMainWindow(parent)
{
    playlist_manager_ = new playlist_manager(this);
    controller_ = new playback_controller(this);

    setup_ui();
    setup_connections();

    controller_->set_spectrum_widget(spectrum_widget_);

    playlist_manager_->load_playlists();
    setWindowTitle("Music Player");
    resize(800, 600);
}

mainwindow::~mainwindow() = default;

void mainwindow::closeEvent(QCloseEvent* event)
{
    playlist_manager_->save_playlists();
    QMainWindow::closeEvent(event);
}

void mainwindow::setup_ui()
{
    auto* central_widget = new QWidget(this);
    central_widget->setObjectName("centralWidget");
    setCentralWidget(central_widget);

    auto* main_layout = new QVBoxLayout(central_widget);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);

    song_tree_widget_ = new QTreeWidget();
    song_tree_widget_->setObjectName("songTreeWidget");
    song_tree_widget_->setColumnCount(1);
    song_tree_widget_->header()->hide();
    song_tree_widget_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    song_tree_widget_->setIndentation(0);

    auto* bottom_container = new QWidget();
    bottom_container->setObjectName("bottomContainer");
    bottom_container->setFixedHeight(130);

    auto* bottom_h_layout = new QHBoxLayout(bottom_container);
    bottom_h_layout->setContentsMargins(5, 5, 5, 5);
    bottom_h_layout->setSpacing(10);

    auto* left_panel = new QWidget();
    auto* left_v_layout = new QVBoxLayout(left_panel);
    left_v_layout->setContentsMargins(0, 0, 0, 0);
    left_v_layout->setSpacing(5);

    auto* display_area = new QWidget();
    auto* left_display_v_layout = new QVBoxLayout(display_area);
    left_display_v_layout->setContentsMargins(0, 0, 0, 0);
    left_display_v_layout->setSpacing(5);

    spectrum_widget_ = new spectrum_widget(this);
    progress_slider_ = new QSlider(Qt::Horizontal);
    progress_slider_->setStyleSheet(R"(
        QSlider::groove:horizontal {
            border: 1px solid #bbb;
            background: white;
            height: 4px;
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #b4b4b4, stop:1 #8f8f8f);
            border: 1px solid #5c5c5c;
            width: 12px;
            margin: -4px 0;
            border-radius: 6px;
        }
    )");

    left_display_v_layout->addWidget(spectrum_widget_, 1);
    left_display_v_layout->addWidget(progress_slider_);

    auto* button_panel = new QWidget();
    auto* grid_layout = new QGridLayout(button_panel);
    grid_layout->setContentsMargins(0, 0, 0, 0);

    stop_button_ = new QPushButton();
    prev_button_ = new QPushButton();
    play_pause_button_ = new QPushButton();
    next_button_ = new QPushButton();
    stop_button_->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    prev_button_->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
    play_pause_button_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    next_button_->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));

    song_title_label_ = new QLabel("Music Player", this);

    time_label_ = new QLabel("00:00 / 00:00", this);
    time_label_->setFixedWidth(100);
    time_label_->setAlignment(Qt::AlignCenter);

    auto* button_container = new QWidget();
    auto* buttons_only_layout = new QHBoxLayout(button_container);
    buttons_only_layout->setContentsMargins(0, 0, 0, 0);
    buttons_only_layout->setSpacing(6);
    buttons_only_layout->addWidget(stop_button_);
    buttons_only_layout->addWidget(prev_button_);
    buttons_only_layout->addWidget(play_pause_button_);
    buttons_only_layout->addWidget(next_button_);

    grid_layout->addWidget(song_title_label_, 0, 0, Qt::AlignLeft);
    grid_layout->addWidget(button_container, 0, 1, Qt::AlignCenter);
    grid_layout->addWidget(time_label_, 0, 2, Qt::AlignRight);

    grid_layout->setColumnStretch(0, 1);
    grid_layout->setColumnStretch(1, 0);
    grid_layout->setColumnStretch(2, 1);

    left_v_layout->addWidget(display_area, 1);
    left_v_layout->addWidget(button_panel);

    volume_meter_ = new volume_meter();
    volume_meter_->setFixedWidth(12);
    volume_meter_->setRange(0, 100);
    volume_meter_->setValue(80);
    volume_meter_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    volume_meter_->setOrientation(Qt::Vertical);

    bottom_h_layout->addWidget(left_panel, 1);
    bottom_h_layout->addWidget(volume_meter_);

    main_layout->addWidget(bottom_container);
    main_layout->addWidget(song_tree_widget_, 1);
}

void mainwindow::setup_connections()
{
    connect(progress_slider_, &QSlider::sliderReleased, this, &mainwindow::on_seek_requested);
    connect(progress_slider_, &QSlider::sliderPressed, this, [this] { is_slider_pressed_ = true; });

    connect(volume_meter_, &volume_meter::value_changed, this, &mainwindow::on_volume_changed);

    connect(play_pause_button_, &QPushButton::clicked, this, &mainwindow::on_play_pause_clicked);
    connect(next_button_, &QPushButton::clicked, this, &mainwindow::on_next_clicked);
    connect(prev_button_, &QPushButton::clicked, this, &mainwindow::on_prev_clicked);
    connect(stop_button_, &QPushButton::clicked, this, &mainwindow::on_stop_clicked);

    connect(song_tree_widget_, &QTreeWidget::itemDoubleClicked, this, &mainwindow::on_tree_item_double_clicked);

    connect(controller_, &playback_controller::track_info_ready, this, &mainwindow::update_track_info);
    connect(controller_, &playback_controller::playback_started, this, &mainwindow::on_playback_started);
    connect(controller_, &playback_controller::progress_updated, this, &mainwindow::update_progress);
    connect(controller_, &playback_controller::playback_finished, this, &mainwindow::handle_playback_finished);
    connect(controller_, &playback_controller::playback_error, this, &mainwindow::handle_playback_error);

    connect(playlist_manager_, &playlist_manager::playlists_changed, this, &mainwindow::rebuild_ui_from_playlists);
}

static QIcon create_music_note_icon()
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(Qt::black, 2));
    painter.drawLine(11, 2, 11, 12);
    painter.drawEllipse(4, 9, 7, 5);
    painter.drawLine(11, 2, 14, 4);
    return QIcon{pixmap};
}

void mainwindow::rebuild_ui_from_playlists()
{
    song_tree_widget_->clear();
    currently_playing_item_ = nullptr;
    for (const auto& playlist : playlist_manager_->get_all_playlists())
    {
        auto* playlist_item = new QTreeWidgetItem(song_tree_widget_);
        playlist_item->setText(0, QString("%1 [%2]").arg(playlist.name).arg(playlist.songs.count()));
        playlist_item->setData(0, Qt::UserRole, playlist.id);
        playlist_item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
        for (const auto& song : playlist.songs)
        {
            auto* song_item = new QTreeWidgetItem(playlist_item);
            song_item->setIcon(0, (create_music_note_icon()));
            song_item->setText(0, song.fileName);
            song_item->setData(0, Qt::UserRole, song.filePath);
        }
        playlist_item->setExpanded(true);
    }
}

void mainwindow::on_tree_item_double_clicked(QTreeWidgetItem* item, int column)
{
    if (item == nullptr || item->parent() == nullptr || column != 0)
    {
        return;
    }
    current_playing_file_path_ = item->data(0, Qt::UserRole).toString();
    controller_->play_file(current_playing_file_path_);
}

void mainwindow::on_play_pause_clicked()
{
    if (current_playing_file_path_.isEmpty())
    {
        if (song_tree_widget_->topLevelItemCount() > 0)
        {
            auto* first_playlist = song_tree_widget_->topLevelItem(0);
            if (first_playlist->childCount() > 0)
            {
                song_tree_widget_->setCurrentItem(first_playlist->child(0));
                on_tree_item_double_clicked(first_playlist->child(0), 0);
            }
        }
    }
    else
    {
        is_paused_ = !is_paused_;
        controller_->pause_resume();
        play_pause_button_->setIcon(style()->standardIcon(is_paused_ ? QStyle::SP_MediaPlay : QStyle::SP_MediaPause));
    }
}

void mainwindow::on_next_clicked()
{
    if (currently_playing_item_ == nullptr)
    {
        return;
    }
    QTreeWidgetItem* next_item = song_tree_widget_->itemBelow(currently_playing_item_);
    if (next_item == nullptr)
    {
        if (song_tree_widget_->topLevelItemCount() > 0)
        {
            auto* first_playlist = song_tree_widget_->topLevelItem(0);
            if (first_playlist != nullptr && first_playlist->childCount() > 0)
            {
                next_item = first_playlist->child(0);
            }
        }
    }
    if (next_item != nullptr && next_item->parent() != nullptr)
    {
        song_tree_widget_->setCurrentItem(next_item);
        on_tree_item_double_clicked(next_item, 0);
    }
}

void mainwindow::on_prev_clicked()
{
    if (currently_playing_item_ == nullptr)
    {
        return;
    }
    QTreeWidgetItem* prev_item = song_tree_widget_->itemAbove(currently_playing_item_);
    if (prev_item->parent() == nullptr || prev_item == nullptr)
    {
        auto* last_playlist = song_tree_widget_->topLevelItem(song_tree_widget_->topLevelItemCount() - 1);
        if (last_playlist != nullptr && last_playlist->childCount() > 0)
        {
            prev_item = last_playlist->child(last_playlist->childCount() - 1);
        }
    }
    if (prev_item != nullptr && prev_item->parent() != nullptr)
    {
        song_tree_widget_->setCurrentItem(prev_item);
        on_tree_item_double_clicked(prev_item, 0);
    }
}

void mainwindow::on_stop_clicked()
{
    controller_->stop();
    play_pause_button_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    is_playing_ = false;
    is_paused_ = false;
    song_title_label_->setText("Music Player");
    time_label_->setText("00:00 / 00:00");
    progress_slider_->setValue(0);
    clear_playing_indicator();
}

void mainwindow::on_volume_changed(int value) { controller_->set_volume(value); }

void mainwindow::update_track_info(qint64 duration_ms)
{
    progress_slider_->setRange(0, static_cast<int>(duration_ms));
    QTime total_time = QTime(0, 0).addMSecs(static_cast<int>(duration_ms));
    QString format = duration_ms >= 3600000 ? "hh:mm:ss" : "mm:ss";
    time_label_->setText(QString("00:00 / %1").arg(total_time.toString(format)));
}

void mainwindow::on_playback_started(const QString& file_path, const QString& file_name)
{
    is_playing_ = true;
    is_paused_ = false;
    play_pause_button_->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    song_title_label_->setText(file_name);
    clear_playing_indicator();

    for (int i = 0; i < song_tree_widget_->topLevelItemCount(); ++i)
    {
        for (int j = 0; j < song_tree_widget_->topLevelItem(i)->childCount(); ++j)
        {
            auto* song_item = song_tree_widget_->topLevelItem(i)->child(j);
            if (song_item->data(0, Qt::UserRole).toString() == file_path)
            {
                currently_playing_item_ = song_item;
                song_item->setForeground(0, QBrush(QColor("#D94600")));
                song_tree_widget_->scrollToItem(song_item, QAbstractItemView::PositionAtCenter);
                return;
            }
        }
    }
}

void mainwindow::update_progress(qint64 current_ms, qint64 total_ms)
{
    if (!is_slider_pressed_)
    {
        progress_slider_->setValue(static_cast<int>(current_ms));
    }
    QTime current_time = QTime(0, 0).addMSecs(static_cast<int>(current_ms));
    QTime total_time = QTime(0, 0).addMSecs(static_cast<int>(total_ms));
    QString format = total_ms >= 3600000 ? "hh:mm:ss" : "mm:ss";

    QString time_str = QString("%1 / %2").arg(current_time.toString(format)).arg(total_time.toString(format));
    time_label_->setText(time_str);
}

void mainwindow::handle_playback_finished()
{
    is_playing_ = false;
    is_paused_ = false;
    play_pause_button_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    on_next_clicked();
}

void mainwindow::clear_playing_indicator()
{
    if (currently_playing_item_ != nullptr)
    {
        currently_playing_item_->setForeground(0, QBrush(palette().text()));
        currently_playing_item_ = nullptr;
    }
}

void mainwindow::on_seek_requested()
{
    is_slider_pressed_ = false;
    if (!current_playing_file_path_.isEmpty())
    {
        controller_->seek(progress_slider_->value());
    }
    else
    {
        progress_slider_->setValue(0);
    }
}

void mainwindow::handle_playback_error(const QString& error_message)
{
    QMessageBox::warning(this, "播放错误", error_message);
    on_stop_clicked();
}
