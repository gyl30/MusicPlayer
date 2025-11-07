#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QTime>
#include <QMessageBox>
#include <QStyle>
#include <QScrollBar>
#include <QEasingCurve>

#include "player_window.h"
#include "playback_controller.h"
#include "spectrum_widget.h"
#include "volumemeter.h"
#include "log.h"

constexpr qint64 LYRIC_PREDICTION_OFFSET_MS = 250;

player_window::player_window(playback_controller* controller, QWidget* parent) : QWidget(parent), controller_(controller)
{
    setup_ui();
    setup_connections();
    setWindowTitle("播放器");
    resize(480, 260);
}

player_window::~player_window() = default;

void player_window::setup_ui()
{
    auto* main_container = new QWidget(this);
    main_container->setObjectName("bottomContainer");
    auto* main_layout = new QHBoxLayout(main_container);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);

    auto* top_layout = new QVBoxLayout(this);
    top_layout->addWidget(main_container);
    top_layout->setContentsMargins(0, 0, 0, 0);

    auto* left_panel = new QWidget();
    left_panel_layout_ = new QVBoxLayout(left_panel);
    left_panel_layout_->setContentsMargins(10, 10, 10, 5);
    left_panel_layout_->setSpacing(5);

    spectrum_widget_ = new spectrum_widget(this);
    spectrum_widget_->setObjectName("spectrumWidget");
    spectrum_widget_->setMinimumHeight(40);

    lyrics_and_cover_container_ = new QWidget();
    auto* lyrics_and_cover_layout = new QHBoxLayout(lyrics_and_cover_container_);
    lyrics_and_cover_layout->setContentsMargins(0, 0, 0, 0);
    lyrics_and_cover_layout->setSpacing(10);

    cover_art_label_ = new QLabel();
    cover_art_label_->setObjectName("coverArtLabel");
    cover_art_label_->setFixedSize(80, 80);
    cover_art_label_->setScaledContents(true);
    cover_art_label_->setStyleSheet("border: 1px solid #E0E0E0; border-radius: 5px;");

    lyrics_list_widget_ = new QListWidget(this);
    lyrics_list_widget_->setObjectName("lyricsListWidget");
    lyrics_list_widget_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lyrics_list_widget_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    lyrics_list_widget_->setSelectionMode(QAbstractItemView::NoSelection);
    lyrics_list_widget_->setFocusPolicy(Qt::NoFocus);
    lyrics_list_widget_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    lyrics_and_cover_layout->addWidget(cover_art_label_);
    lyrics_and_cover_layout->addWidget(lyrics_list_widget_, 1);
    lyrics_scroll_animation_ = new QPropertyAnimation(lyrics_list_widget_->verticalScrollBar(), "value", this);

    track_title_label_ = new QLabel("欢迎使用", this);
    track_title_label_->setAlignment(Qt::AlignCenter);
    track_title_label_->hide();

    progress_slider_ = new QSlider(Qt::Horizontal);
    time_label_ = new QLabel("00:00 / 00:00", this);
    time_label_->setFixedWidth(120);
    time_label_->setAlignment(Qt::AlignCenter);

    auto* progress_layout = new QHBoxLayout();
    progress_layout->addWidget(progress_slider_);
    progress_layout->addWidget(time_label_);

    stop_button_ = new QPushButton(QIcon(":/icons/stop.svg"), "");
    prev_button_ = new QPushButton(QIcon(":/icons/previous.svg"), "");
    play_pause_button_ = new QPushButton(QIcon(":/icons/play.svg"), "");
    next_button_ = new QPushButton(QIcon(":/icons/next.svg"), "");
    shuffle_button_ = new QPushButton(QIcon(":/icons/repeat.svg"), "");
    shuffle_button_->setObjectName("modeButton");

    QSize icon_size(24, 24);
    stop_button_->setIconSize(icon_size);
    prev_button_->setIconSize(icon_size);
    play_pause_button_->setIconSize(QSize(28, 28));
    next_button_->setIconSize(icon_size);
    shuffle_button_->setIconSize(icon_size);

    stop_button_->setToolTip("停止");
    prev_button_->setToolTip("上一首");
    play_pause_button_->setToolTip("播放/暂停");
    next_button_->setToolTip("下一首");
    shuffle_button_->setToolTip("列表循环");

    auto* controls_container = new QWidget();
    auto* controls_layout = new QHBoxLayout(controls_container);
    controls_layout->setContentsMargins(0, 0, 0, 0);
    controls_layout->setSpacing(10);
    controls_layout->addStretch();
    controls_layout->addWidget(shuffle_button_);
    controls_layout->addWidget(prev_button_);
    controls_layout->addWidget(play_pause_button_);
    controls_layout->addWidget(next_button_);
    controls_layout->addWidget(stop_button_);
    controls_layout->addStretch();

    left_panel_layout_->addWidget(spectrum_widget_);
    left_panel_layout_->addWidget(lyrics_and_cover_container_);
    left_panel_layout_->addWidget(track_title_label_);
    left_panel_layout_->addLayout(progress_layout);
    left_panel_layout_->addWidget(controls_container);

    left_panel_layout_->setStretchFactor(spectrum_widget_, 1);
    left_panel_layout_->setStretchFactor(lyrics_and_cover_container_, 1);

    volume_meter_ = new volume_meter();
    volume_meter_->setFixedWidth(8);
    volume_meter_->setRange(0, 100);
    volume_meter_->setValue(80);
    volume_meter_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    volume_meter_->setOrientation(Qt::Vertical);
    volume_meter_->setToolTip("音量");

    main_layout->addWidget(left_panel, 1);
    main_layout->addWidget(volume_meter_);

    update_media_display_layout();
}

void player_window::setup_connections()
{
    connect(progress_slider_, &QSlider::sliderReleased, this, &player_window::on_seek_requested);
    connect(progress_slider_, &QSlider::sliderPressed, this, [this] { is_slider_pressed_ = true; });

    connect(play_pause_button_, &QPushButton::clicked, this, &player_window::on_play_pause_clicked);
    connect(next_button_, &QPushButton::clicked, this, &player_window::on_next_clicked);
    connect(prev_button_, &QPushButton::clicked, this, &player_window::on_prev_clicked);
    connect(stop_button_, &QPushButton::clicked, this, &player_window::on_stop_clicked);
    connect(shuffle_button_, &QPushButton::clicked, this, &player_window::on_playback_mode_clicked);
    connect(volume_meter_, &volume_meter::value_changed, this, &player_window::on_volume_changed);

    if (controller_ != nullptr)
    {
        connect(this, &player_window::playback_mode_changed, controller_, &playback_controller::set_playback_mode);

        connect(controller_, &playback_controller::track_info_ready, this, &player_window::update_track_info);
        connect(controller_, &playback_controller::playback_started, this, &player_window::on_playback_started);
        connect(controller_, &playback_controller::progress_updated, this, &player_window::update_progress);
        connect(controller_, &playback_controller::playback_error, this, &player_window::handle_playback_error);
        connect(controller_, &playback_controller::metadata_ready, this, &player_window::on_metadata_updated);
        connect(controller_, &playback_controller::cover_art_ready, this, &player_window::on_cover_art_updated);
        connect(controller_, &playback_controller::lyrics_updated, this, &player_window::on_lyrics_updated);
        connect(controller_, &playback_controller::playback_finished, this, &player_window::on_playback_finished);
        connect(controller_, &playback_controller::playback_paused, this, &player_window::on_playback_paused);

        controller_->set_spectrum_widget(spectrum_widget_);
        controller_->set_volume(volume_meter_->value());
    }
}

void player_window::reset_ui()
{
    is_paused_ = false;
    play_pause_button_->setIcon(QIcon(":/icons/play.svg"));
    track_title_label_->setText("欢迎使用");
    time_label_->setText("00:00 / 00:00");
    progress_slider_->setValue(0);

    has_cover_art_ = false;
    has_lyrics_ = false;
    lyrics_list_widget_->clear();
    current_lyrics_.clear();
    cover_art_label_->clear();
    update_media_display_layout();
}

void player_window::on_playback_stopped()
{
    reset_ui();
    hide();
}

void player_window::on_playback_finished() { reset_ui(); }

void player_window::on_playback_paused(bool is_paused)
{
    is_paused_ = is_paused;
    play_pause_button_->setIcon(is_paused_ ? QIcon(":/icons/play.svg") : QIcon(":/icons/pause.svg"));
}

void player_window::update_media_display_layout()
{
    if (has_lyrics_)
    {
        lyrics_and_cover_container_->show();
        if (has_cover_art_)
        {
            cover_art_label_->show();
        }
        else
        {
            cover_art_label_->hide();
        }
        left_panel_layout_->setStretchFactor(spectrum_widget_, 1);
        left_panel_layout_->setStretchFactor(lyrics_and_cover_container_, 1);
    }
    else
    {
        lyrics_and_cover_container_->hide();
        cover_art_label_->hide();
        left_panel_layout_->setStretchFactor(spectrum_widget_, 1);
        left_panel_layout_->setStretchFactor(lyrics_and_cover_container_, 0);
    }
}

void player_window::on_play_pause_clicked()
{
    if (controller_ != nullptr)
    {
        controller_->pause_resume();
    }
}

void player_window::on_next_clicked() { emit next_requested(); }

void player_window::on_prev_clicked() { emit previous_requested(); }

void player_window::on_stop_clicked() { emit stop_requested(); }

void player_window::on_volume_changed(int value)
{
    if (controller_ != nullptr)
    {
        controller_->set_volume(value);
    }
}

void player_window::on_playback_started(const QString& file_path, const QString& file_name)
{
    (void)file_path;
    reset_ui();
    is_paused_ = false;
    play_pause_button_->setIcon(QIcon(":/icons/pause.svg"));
    track_title_label_->setText(file_name);
    setWindowTitle(file_name);
}

void player_window::on_cover_art_updated(const QByteArray& image_data)
{
    QPixmap cover_pixmap;
    if (cover_pixmap.loadFromData(image_data))
    {
        cover_art_label_->setPixmap(cover_pixmap);
        has_cover_art_ = true;
    }
    else
    {
        LOG_WARN("无法从数据加载封面图片");
        has_cover_art_ = false;
    }
    update_media_display_layout();
}

void player_window::on_lyrics_updated(const QList<LyricLine>& lyrics)
{
    current_lyrics_ = lyrics;
    current_lyric_index_ = -1;
    lyrics_list_widget_->clear();

    if (current_lyrics_.isEmpty())
    {
        has_lyrics_ = false;
    }
    else
    {
        has_lyrics_ = true;
        for (const auto& line : current_lyrics_)
        {
            auto* item = new QListWidgetItem(line.text, lyrics_list_widget_);
            item->setTextAlignment(Qt::AlignCenter);
        }
    }
    update_media_display_layout();
}

void player_window::on_playback_mode_clicked()
{
    switch (current_mode_)
    {
        case playback_mode::ListLoop:
            current_mode_ = playback_mode::SingleLoop;
            break;
        case playback_mode::SingleLoop:
            current_mode_ = playback_mode::Shuffle;
            break;
        case playback_mode::Shuffle:
            current_mode_ = playback_mode::Sequential;
            break;
        case playback_mode::Sequential:
            current_mode_ = playback_mode::ListLoop;
            break;
    }
    update_playback_mode_button_style();
    emit playback_mode_changed(current_mode_);
}

void player_window::update_playback_mode_button_style()
{
    switch (current_mode_)
    {
        case playback_mode::ListLoop:
            shuffle_button_->setIcon(QIcon(":/icons/repeat.svg"));
            shuffle_button_->setToolTip("列表循环");
            break;
        case playback_mode::SingleLoop:
            shuffle_button_->setIcon(QIcon(":/icons/repeat-one.svg"));
            shuffle_button_->setToolTip("单曲循环");
            break;
        case playback_mode::Shuffle:
            shuffle_button_->setIcon(QIcon(":/icons/shuffle.svg"));
            shuffle_button_->setToolTip("随机播放");
            break;
        case playback_mode::Sequential:
            shuffle_button_->setIcon(QIcon(":/icons/sequential.svg"));
            shuffle_button_->setToolTip("顺序播放");
            break;
    }
    shuffle_button_->setProperty("active", current_mode_ != playback_mode::ListLoop);
    style()->unpolish(shuffle_button_);
    style()->polish(shuffle_button_);
}

void player_window::update_track_info(qint64 duration_ms)
{
    progress_slider_->setRange(0, static_cast<int>(duration_ms));
    QTime total_time = QTime(0, 0).addMSecs(static_cast<int>(duration_ms));
    QString format = duration_ms >= 3600000 ? "hh:mm:ss" : "mm:ss";
    QString current_time_str = QTime(0, 0).toString(format);
    time_label_->setText(QString("%1 / %2").arg(current_time_str).arg(total_time.toString(format)));
}

void player_window::update_progress(qint64 current_ms, qint64 total_ms)
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

    if (current_lyrics_.isEmpty())
    {
        return;
    }

    const qint64 predicted_ms = current_ms + LYRIC_PREDICTION_OFFSET_MS;
    int new_lyric_index = -1;
    for (int i = 0; i < current_lyrics_.size(); ++i)
    {
        if (predicted_ms >= current_lyrics_[i].timestamp_ms)
        {
            new_lyric_index = i;
        }
        else
        {
            break;
        }
    }

    if (new_lyric_index != -1 && new_lyric_index != current_lyric_index_)
    {
        if (lyrics_scroll_animation_->state() == QAbstractAnimation::Running)
        {
            lyrics_scroll_animation_->stop();
        }

        if (current_lyric_index_ >= 0 && current_lyric_index_ < lyrics_list_widget_->count())
        {
            lyrics_list_widget_->item(current_lyric_index_)->setSelected(false);
        }

        auto* new_item = lyrics_list_widget_->item(new_lyric_index);
        if (new_item != nullptr)
        {
            new_item->setSelected(true);
            lyrics_list_widget_->scrollToItem(new_item, QAbstractItemView::PositionAtCenter);
        }
        current_lyric_index_ = new_lyric_index;
    }
}

void player_window::handle_playback_error(const QString& error_message)
{
    QMessageBox::warning(this, "播放错误", error_message);
    emit stop_requested();
}

void player_window::on_metadata_updated(const QMap<QString, QString>& metadata)
{
    QString title = metadata.value("title");
    QString artist = metadata.value("artist");
    QString display_text;

    if (!artist.isEmpty() && !title.isEmpty())
    {
        display_text = QString("%1 - %2").arg(artist, title);
    }
    else if (!title.isEmpty())
    {
        display_text = title;
    }
    else
    {
        return;
    }
    track_title_label_->setText(display_text);
}

void player_window::on_seek_requested()
{
    is_slider_pressed_ = false;
    if (controller_ != nullptr && controller_->is_media_loaded())
    {
        controller_->seek(progress_slider_->value());
    }
    else
    {
        progress_slider_->setValue(0);
    }
}
