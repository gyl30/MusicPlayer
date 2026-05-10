#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QTime>
#include <QSettings>
#include <QStyle>
#include <QResizeEvent>
#include <QFontMetrics>

#include "volumemeter.h"
#include "player_window.h"
#include "playlist_window.h"
#include "playback_controller.h"

player_window::player_window(playback_controller* controller, playlist_window* main_wnd)
    : QWidget(main_wnd), controller_(controller)
{
    setup_ui();
    setup_connections();
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(96);
}

player_window::~player_window() = default;

void player_window::set_playback_mode(playback_mode mode)
{
    if (current_mode_ == mode)
    {
        update_playback_mode_button_style();
        return;
    }

    current_mode_ = mode;
    update_playback_mode_button_style();
    emit playback_mode_changed(current_mode_);
}

void player_window::restore_idle_state(const QString& title, qint64 position_ms)
{
    is_paused_ = false;
    play_pause_button_->setIcon(QIcon(":/icons/play.svg"));
    set_track_title(title);

    const qint64 safe_position_ms = qMax<qint64>(0, position_ms);
    progress_slider_->setRange(0, static_cast<int>(safe_position_ms));
    progress_slider_->setValue(static_cast<int>(safe_position_ms));

    const QTime restored_time = QTime(0, 0).addMSecs(static_cast<int>(safe_position_ms));
    const QString format = safe_position_ms >= 3600000 ? "hh:mm:ss" : "mm:ss";
    const QString time_text = QString("%1 / --:--").arg(restored_time.toString(format));
    set_time_text(time_text);

    lyrics_.clear();
    current_lyric_status_.clear();
    emit lyric_status_changed(QString());
}

void player_window::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    refresh_time_label_width();
    refresh_track_title_elision();
}

void player_window::setup_ui()
{
    main_container_ = new QWidget(this);
    main_container_->setObjectName("playerPanel");
    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(3);
    root_layout->addWidget(main_container_);

    left_panel_layout_ = new QVBoxLayout(main_container_);
    left_panel_layout_->setContentsMargins(8, 8, 8, 6);
    left_panel_layout_->setSpacing(7);

    auto* title_layout = new QHBoxLayout();
    title_layout->setContentsMargins(0, 0, 0, 0);
    title_layout->setSpacing(8);

    track_title_label_ = new QLabel("欢迎使用", this);
    track_title_label_->setObjectName("trackTitleLabel");
    track_title_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    track_title_label_->setMinimumWidth(0);
    track_title_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    title_layout->addWidget(track_title_label_, 1);

    time_label_ = new QLabel("00:00 / 00:00", this);
    time_label_->setObjectName("timeLabel");
    time_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    time_label_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    refresh_time_label_width();
    title_layout->addWidget(time_label_);

    progress_slider_ = new QSlider(Qt::Horizontal);
    progress_slider_->setObjectName("progressSlider");

    auto* progress_layout = new QHBoxLayout();
    progress_layout->setContentsMargins(0, 0, 0, 0);
    progress_layout->setSpacing(0);
    progress_layout->addWidget(progress_slider_);

    stop_button_ = new QPushButton(QIcon(":/icons/stop.svg"), "");
    prev_button_ = new QPushButton(QIcon(":/icons/previous.svg"), "");
    play_pause_button_ = new QPushButton(QIcon(":/icons/play.svg"), "");
    play_pause_button_->setObjectName("playPauseButton");
    next_button_ = new QPushButton(QIcon(":/icons/next.svg"), "");
    shuffle_button_ = new QPushButton(QIcon(":/icons/repeat.svg"), "");
    shuffle_button_->setObjectName("modeButton");

    stop_button_->setToolTip("停止");
    prev_button_->setToolTip("上一首");
    play_pause_button_->setToolTip("播放/暂停");
    next_button_->setToolTip("下一首");
    shuffle_button_->setToolTip("列表循环");

    auto* controls_container = new QWidget();
    controls_container->setObjectName("controlsContainer");
    auto* controls_layout = new QHBoxLayout(controls_container);
    controls_layout->setContentsMargins(0, 0, 0, 0);
    controls_layout->setSpacing(4);

    controls_layout->addWidget(shuffle_button_);
    controls_layout->addWidget(prev_button_);
    controls_layout->addWidget(play_pause_button_);
    controls_layout->addWidget(next_button_);
    controls_layout->addWidget(stop_button_);
    controls_layout->addStretch();

    volume_meter_ = new volume_meter();
    volume_meter_->setObjectName("volumeMeter");
    volume_meter_->setRange(0, 100);
    QSettings settings("MusicPlayer", "MusicPlayer");
    const int saved_volume = qBound(0, settings.value("playback/volume", 80).toInt(), 100);
    volume_meter_->setValue(saved_volume);
    volume_meter_->setFixedSize(68, 10);
    volume_meter_->setOrientation(Qt::Horizontal);
    volume_meter_->setToolTip("音量");

    auto* volume_label = new QLabel("音量", this);
    volume_label->setObjectName("volumeLabel");
    controls_layout->addWidget(volume_label);
    controls_layout->addWidget(volume_meter_);

    left_panel_layout_->addLayout(title_layout);
    left_panel_layout_->addLayout(progress_layout);
    root_layout->addWidget(controls_container);
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

        controller_->set_volume(volume_meter_->value());
    }
}

void player_window::reset_ui()
{
    is_paused_ = false;
    play_pause_button_->setIcon(QIcon(":/icons/play.svg"));
    set_track_title("欢迎使用");
    set_time_text("00:00 / 00:00");
    progress_slider_->setValue(0);
    lyrics_.clear();
    current_lyric_status_.clear();
    emit lyric_status_changed(QString());
}

void player_window::on_playback_stopped()
{
    reset_ui();
}

void player_window::on_playback_finished() { reset_ui(); }

void player_window::on_playback_paused(bool is_paused)
{
    is_paused_ = is_paused;
    play_pause_button_->setIcon(is_paused_ ? QIcon(":/icons/play.svg") : QIcon(":/icons/pause.svg"));
}

void player_window::on_play_pause_clicked()
{
    if (controller_ != nullptr)
    {
        if (!controller_->is_media_loaded())
        {
            emit play_requested();
            return;
        }
        controller_->pause_resume();
    }
}

void player_window::on_next_clicked() { emit next_requested(); }

void player_window::on_prev_clicked() { emit previous_requested(); }

void player_window::on_stop_clicked() { emit stop_requested(); }

void player_window::on_volume_changed(int value)
{
    QSettings settings("MusicPlayer", "MusicPlayer");
    settings.setValue("playback/volume", value);

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
    set_track_title(file_name);
    setWindowTitle(file_name);
}

void player_window::on_cover_art_updated(const QByteArray& image_data) { (void)image_data; }

void player_window::on_lyrics_updated(const QList<LyricLine>& lyrics)
{
    lyrics_ = lyrics;
    current_lyric_status_.clear();
    emit lyric_status_changed(QString());
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
    set_time_text(QString("%1 / %2").arg(current_time_str).arg(total_time.toString(format)));
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
    set_time_text(time_str);

    const QString lyric_status = lyric_at_time(current_ms);
    if (lyric_status != current_lyric_status_)
    {
        current_lyric_status_ = lyric_status;
        emit lyric_status_changed(current_lyric_status_);
    }
}

QString player_window::lyric_at_time(qint64 time_ms) const
{
    if (lyrics_.isEmpty())
    {
        return {};
    }

    int matched_index = -1;
    for (int i = 0; i < lyrics_.size(); ++i)
    {
        if (time_ms >= lyrics_[i].timestamp_ms)
        {
            matched_index = i;
        }
        else
        {
            break;
        }
    }

    if (matched_index < 0)
    {
        return {};
    }

    return lyrics_[matched_index].text.trimmed();
}

void player_window::handle_playback_error(const QString& error_message) { set_track_title(QString("错误: %1").arg(error_message)); }

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
    set_track_title(display_text);
}

void player_window::set_track_title(const QString& title)
{
    full_track_title_ = title;
    track_title_label_->setToolTip(title);
    refresh_track_title_elision();
}

void player_window::set_time_text(const QString& text)
{
    if (time_label_ == nullptr)
    {
        return;
    }

    if (time_label_->text() == text)
    {
        return;
    }

    time_label_->setText(text);
    refresh_time_label_width();
    refresh_track_title_elision();
}

void player_window::refresh_time_label_width()
{
    if (time_label_ == nullptr)
    {
        return;
    }

    const QFontMetrics metrics(time_label_->font());
    time_label_->setFixedWidth(metrics.horizontalAdvance(time_label_->text()) + 10);
}

void player_window::refresh_track_title_elision()
{
    if (track_title_label_ == nullptr)
    {
        return;
    }

    const int available_width = track_title_label_->width();
    if (available_width <= 0)
    {
        track_title_label_->setText(full_track_title_);
        return;
    }

    const QFontMetrics metrics(track_title_label_->font());
    track_title_label_->setText(metrics.elidedText(full_track_title_, Qt::ElideRight, available_width));
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
