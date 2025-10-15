#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMediaDevices>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>
#include <QVBoxLayout>
#include <QUuid>

#include "log.h"
#include "mainwindow.h"
#include "audio_player.h"
#include "audio_decoder.h"
#include "spectrum_widget.h"

const static auto kBufferHighWatermarkSeconds = 5L;

static QString format_time(qint64 time_ms)
{
    int total_seconds = static_cast<int>(time_ms) / 1000;
    int minutes = total_seconds / 60;
    int seconds = total_seconds % 60;
    return QString("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
}

static QAudioFormat default_audio_format()
{
    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);
    return format;
}

mainwindow::mainwindow(QWidget* parent) : QMainWindow(parent)
{
    qRegisterMetaType<std::shared_ptr<audio_packet>>("std::shared_ptr<audio_packet>");

    decoder_thread_ = new QThread(this);
    decoder_ = new audio_decoder();
    decoder_->moveToThread(decoder_thread_);
    decoder_thread_->start();

    setup_ui();
    setup_connections();

    qApp->installEventFilter(this);

    const QString app_data_path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(app_data_path);
    if (!dir.exists())
    {
        dir.mkpath(".");
    }
    playlist_path_ = app_data_path + "/playlists.txt";
    load_playlist();

    setWindowTitle("音乐播放器");
    resize(1000, 700);
}

mainwindow::~mainwindow()
{
    qApp->removeEventFilter(this);
    stop_playback();

    decoder_thread_->quit();
    decoder_thread_->wait();

    cleanup_player();
}

void mainwindow::cleanup_player()
{
    if (player_thread_ != nullptr)
    {
        if (player_ != nullptr)
        {
            QMetaObject::invokeMethod(player_, "stop_playback", Qt::BlockingQueuedConnection);
        }

        player_thread_->quit();
        player_thread_->wait();
        player_thread_->deleteLater();
        player_thread_ = nullptr;
        player_ = nullptr;
    }
}

void mainwindow::closeEvent(QCloseEvent* event)
{
    save_playlist();
    QMainWindow::closeEvent(event);
}

void mainwindow::keyPressEvent(QKeyEvent* event)
{
    QListWidget* current_list = current_song_list_widget();
    if (event->key() == Qt::Key_Delete && current_list != nullptr && current_list->hasFocus())
    {
        on_playlist_context_menu_requested(QPoint());
    }
    else
    {
        QMainWindow::keyPressEvent(event);
    }
}

bool mainwindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress)
    {
        if (currently_editing_ != nullptr)
        {
            if (!currently_editing_->rect().contains(currently_editing_->mapFromGlobal(QCursor::pos())))
            {
                finish_playlist_edit();
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void mainwindow::setup_ui()
{
    player_view_widget_ = new QWidget();
    auto* player_view_layout = new QHBoxLayout(player_view_widget_);
    player_view_layout->setContentsMargins(0, 0, 0, 0);
    player_view_layout->setSpacing(0);

    auto* nav_container = new QWidget();
    nav_container->setObjectName("navContainer");
    nav_container->setMaximumWidth(200);
    playlist_nav_layout_ = new QVBoxLayout(nav_container);
    playlist_nav_layout_->setSpacing(5);

    nav_separator_ = new QFrame();
    nav_separator_->setFrameShape(QFrame::HLine);
    nav_separator_->setFrameShadow(QFrame::Sunken);
    playlist_nav_layout_->addWidget(nav_separator_);

    management_button_ = new QPushButton("音乐管理");
    management_button_->setObjectName("managementButton");
    playlist_nav_layout_->addWidget(management_button_);

    add_playlist_button_ = new QPushButton("+ 新建播放列表");
    add_playlist_button_->setObjectName("addButton");
    playlist_nav_layout_->addWidget(add_playlist_button_);
    playlist_nav_layout_->addStretch();

    auto* player_content_splitter = new QSplitter(Qt::Horizontal);
    playlist_stack_ = new QStackedWidget();
    auto* right_column_widget = new QWidget();
    auto* right_column_layout = new QVBoxLayout(right_column_widget);
    spectrum_widget_ = new spectrum_widget;
    progress_slider_ = new QSlider(Qt::Horizontal);
    time_label_ = new QLabel("00:00 / 00:00");
    auto* progress_layout = new QHBoxLayout;
    progress_layout->addWidget(progress_slider_);
    progress_layout->addWidget(time_label_);
    right_column_layout->addWidget(spectrum_widget_);
    right_column_layout->addLayout(progress_layout);

    player_content_splitter->addWidget(playlist_stack_);
    player_content_splitter->addWidget(right_column_widget);
    player_content_splitter->setSizes({200, 600});
    player_content_splitter->setStretchFactor(0, 1);
    player_content_splitter->setStretchFactor(1, 2);

    player_view_layout->addWidget(nav_container);
    player_view_layout->addWidget(player_content_splitter);
    player_view_widget_->setLayout(player_view_layout);

    management_view_widget_ = new QWidget();
    auto* mgmt_main_layout = new QHBoxLayout(management_view_widget_);
    mgmt_main_layout->setSpacing(10);

    auto* left_mgmt_splitter = new QSplitter(Qt::Vertical);
    mgmt_source_playlists_ = new QListWidget();
    mgmt_source_songs_ = new QListWidget();
    mgmt_source_songs_->setSelectionMode(QAbstractItemView::NoSelection);
    left_mgmt_splitter->addWidget(mgmt_source_playlists_);
    left_mgmt_splitter->addWidget(mgmt_source_songs_);

    auto* middle_controls_widget = new QWidget();
    auto* middle_layout = new QVBoxLayout(middle_controls_widget);
    add_songs_to_playlist_button_ = new QPushButton("->");
    finish_management_button_ = new QPushButton("完成");
    middle_layout->addStretch();
    middle_layout->addWidget(add_songs_to_playlist_button_);
    middle_layout->addWidget(finish_management_button_);
    middle_layout->addStretch();
    middle_controls_widget->setLayout(middle_layout);

    auto* right_mgmt_splitter = new QSplitter(Qt::Vertical);
    mgmt_dest_playlists_ = new QListWidget();
    mgmt_dest_songs_ = new QListWidget();
    right_mgmt_splitter->addWidget(mgmt_dest_playlists_);
    right_mgmt_splitter->addWidget(mgmt_dest_songs_);

    mgmt_main_layout->addWidget(left_mgmt_splitter, 1);
    mgmt_main_layout->addWidget(middle_controls_widget, 0);
    mgmt_main_layout->addWidget(right_mgmt_splitter, 1);

    main_stack_widget_ = new QStackedWidget();
    main_stack_widget_->addWidget(player_view_widget_);
    main_stack_widget_->addWidget(management_view_widget_);

    setCentralWidget(main_stack_widget_);
}

void mainwindow::setup_connections()
{
    connect(add_playlist_button_, &QPushButton::clicked, this, &mainwindow::add_new_playlist);
    connect(management_button_, &QPushButton::clicked, this, &mainwindow::show_management_view);

    connect(progress_slider_, &QSlider::sliderMoved, this, &mainwindow::on_progress_slider_moved);
    connect(progress_slider_, &QSlider::sliderPressed, this, [this] { is_slider_pressed_ = true; });
    connect(progress_slider_, &QSlider::sliderReleased, this, &mainwindow::on_seek_requested);

    connect(finish_management_button_, &QPushButton::clicked, this, &mainwindow::show_player_view);
    connect(add_songs_to_playlist_button_, &QPushButton::clicked, this, &mainwindow::add_selected_songs_to_playlist);
    connect(mgmt_source_playlists_, &QListWidget::currentItemChanged, this, &mainwindow::update_management_source_songs);
    connect(mgmt_dest_playlists_, &QListWidget::currentItemChanged, this, &mainwindow::update_management_dest_songs);

    connect(this, &mainwindow::request_decoding, decoder_, &audio_decoder::start_decoding);
    connect(this, &mainwindow::request_resume_decoding, decoder_, &audio_decoder::resume_decoding, Qt::QueuedConnection);
    connect(this, &mainwindow::request_stop_decoding, decoder_, &audio_decoder::shutdown, Qt::QueuedConnection);
    connect(this, &mainwindow::request_seek, decoder_, &audio_decoder::seek, Qt::QueuedConnection);

    connect(decoder_, &audio_decoder::duration_ready, this, &mainwindow::on_duration_ready, Qt::QueuedConnection);
    connect(decoder_, &audio_decoder::packet_ready, this, &mainwindow::on_packet_from_decoder, Qt::QueuedConnection);
    connect(decoder_, &audio_decoder::seek_finished, this, &mainwindow::on_seek_finished, Qt::QueuedConnection);
    connect(decoder_, &audio_decoder::decoding_error, this, &mainwindow::on_decoding_error, Qt::QueuedConnection);

    connect(decoder_thread_, &QThread::finished, decoder_, &QObject::deleteLater);
}

void mainwindow::on_list_double_clicked(QListWidgetItem* item)
{
    if (item == nullptr)
    {
        return;
    }
    if (currently_editing_ != nullptr)
    {
        finish_playlist_edit();
    }

    LOG_DEBUG("requesting playback {}", item->data(Qt::UserRole).toString().toStdString());

    stop_playback();

    current_session_id_ = ++session_id_counter_;
    current_playing_file_path_ = item->data(Qt::UserRole).toString();
    LOG_INFO("session {} starting new playback for file {}", current_session_id_, current_playing_file_path_.toStdString());
    emit request_decoding(current_session_id_, current_playing_file_path_, default_audio_format(), -1);

    auto* list = item->listWidget();
    for (const auto& p_id : playlists_.keys())
    {
        if (playlists_[p_id].widget == list)
        {
            switch_to_playlist(p_id);
            break;
        }
    }
}

void mainwindow::stop_playback()
{
    if (!is_media_loaded_)
    {
        return;
    }

    LOG_INFO("session {} stopping playback and cleaning up resources", current_session_id_);
    is_playing_ = false;
    is_media_loaded_ = false;

    buffered_bytes_ = 0;
    decoder_is_waiting_ = false;
    current_session_id_ = 0;

    emit request_stop_decoding();

    cleanup_player();

    spectrum_widget_->stop_playback();
    progress_slider_->setValue(0);
    time_label_->setText("00:00 / 00:00");
    total_duration_ms_ = 0;

    is_seeking_ = false;
    pending_seek_ms_ = -1;
}

void mainwindow::on_duration_ready(qint64 session_id, qint64 duration_ms, const QAudioFormat& format)
{
    if (session_id != current_session_id_)
    {
        LOG_WARN("session {} ignoring duration_ready for obsolete session current is {}", session_id, current_session_id_);
        return;
    }

    LOG_INFO("session {} decoder ready duration {}ms creating player", session_id, duration_ms);
    total_duration_ms_ = duration_ms;
    progress_slider_->setRange(0, static_cast<int>(total_duration_ms_));
    time_label_->setText(QString("00:00 / %1").arg(format_time(total_duration_ms_)));

    is_media_loaded_ = true;

    cleanup_player();

    buffered_bytes_ = 0;
    decoder_is_waiting_ = false;

    buffer_high_water_mark_ = kBufferHighWatermarkSeconds * format.bytesPerFrame() * format.sampleRate();
    LOG_INFO("session {} buffer high water mark set to {} bytes {} seconds", session_id, buffer_high_water_mark_, kBufferHighWatermarkSeconds);

    player_thread_ = new QThread(this);
    player_ = new audio_player();
    player_->moveToThread(player_thread_);

    connect(player_, &audio_player::progress_update, this, &mainwindow::on_progress_update, Qt::QueuedConnection);
    connect(player_, &audio_player::playback_finished, this, &mainwindow::on_playback_finished, Qt::QueuedConnection);
    connect(player_, &audio_player::playback_ready, this, &mainwindow::on_player_ready, Qt::QueuedConnection);
    connect(player_, &audio_player::playback_error, this, &mainwindow::on_player_error, Qt::QueuedConnection);
    connect(player_, &audio_player::packet_played, this, &mainwindow::on_packet_for_spectrum, Qt::QueuedConnection);

    connect(player_thread_, &QThread::finished, player_, &QObject::deleteLater);
    player_thread_->start();
    player_thread_->setPriority(QThread::TimeCriticalPriority);

    LOG_DEBUG("session {} requesting player to prepare for playback", session_id);
    QMetaObject::invokeMethod(
        player_, "start_playback", Qt::QueuedConnection, Q_ARG(qint64, session_id), Q_ARG(QAudioFormat, format), Q_ARG(qint64, 0));
}

void mainwindow::on_player_ready(qint64 session_id)
{
    if (session_id != current_session_id_)
    {
        LOG_WARN("session {} ignoring player_ready for obsolete session current is {}", session_id, current_session_id_);
        return;
    }
    LOG_INFO("session {} player is ready requesting decoder to resume", session_id);
    is_playing_ = true;
    spectrum_widget_->start_playback();

    emit request_resume_decoding();
}

void mainwindow::on_player_error(const QString& error_message)
{
    QMessageBox::critical(this, "播放错误", error_message);
    stop_playback();
}

void mainwindow::on_packet_from_decoder(qint64 session_id, const std::shared_ptr<audio_packet>& packet)
{
    if (session_id != current_session_id_ && packet != nullptr)
    {
        return;
    }

    if (player_ != nullptr)
    {
        if (packet)
        {
            buffered_bytes_ += static_cast<qint64>(packet->data.size());
        }

        QMetaObject::invokeMethod(
            player_, "enqueue_packet", Qt::QueuedConnection, Q_ARG(qint64, session_id), Q_ARG(std::shared_ptr<audio_packet>, packet));
    }

    if (packet != nullptr && is_playing_ && !is_seeking_)
    {
        if (buffered_bytes_ < buffer_high_water_mark_)
        {
            emit request_resume_decoding();
        }
        else
        {
            LOG_TRACE("session {} buffer is full {} bytes decoder now waiting", session_id, buffered_bytes_.load());
            decoder_is_waiting_ = true;
        }
    }
}

void mainwindow::on_packet_for_spectrum(const std::shared_ptr<audio_packet>& packet)
{
    if (spectrum_widget_ != nullptr && is_playing_)
    {
        buffered_bytes_ -= static_cast<qint64>(packet->data.size());

        spectrum_widget_->enqueue_packet(packet);

        if (decoder_is_waiting_ && is_playing_ && !is_seeking_)
        {
            if (buffered_bytes_ < buffer_high_water_mark_)
            {
                LOG_TRACE("session {} buffer has space {} bytes waking up decoder", current_session_id_, buffered_bytes_.load());
                decoder_is_waiting_ = false;
                emit request_resume_decoding();
            }
        }
    }
}

void mainwindow::on_progress_update(qint64 session_id, qint64 current_ms)
{
    if (session_id != current_session_id_ || !is_playing_)
    {
        return;
    }
    if (!is_slider_pressed_)
    {
        progress_slider_->setValue(static_cast<int>(current_ms));
    }
    time_label_->setText(QString("%1 / %2").arg(format_time(current_ms)).arg(format_time(total_duration_ms_)));
}

void mainwindow::on_playback_finished(qint64 session_id)
{
    if (session_id != current_session_id_)
    {
        LOG_INFO("session {} ignoring playback_finished for obsolete session current is {}", session_id, current_session_id_);
        return;
    }
    LOG_INFO("session {} playback finished now paused at end", session_id);

    is_playing_ = false;
    spectrum_widget_->stop_playback();

    if (total_duration_ms_ > 0)
    {
        progress_slider_->setValue(static_cast<int>(total_duration_ms_));
        time_label_->setText(QString("%1 / %2").arg(format_time(total_duration_ms_)).arg(format_time(total_duration_ms_)));
    }
}

void mainwindow::on_decoding_error(const QString& error_message)
{
    QMessageBox::critical(this, "解码错误", error_message);
    stop_playback();
}

void mainwindow::on_seek_requested()
{
    is_slider_pressed_ = false;
    if (!is_media_loaded_)
    {
        return;
    }

    qint64 position_ms = progress_slider_->value();

    if (is_seeking_)
    {
        LOG_INFO("session {} seek is busy pending new request to {}ms", current_session_id_, position_ms);
        pending_seek_ms_ = position_ms;
        return;
    }

    LOG_INFO("session {} starting seek to {}ms", current_session_id_, position_ms);
    is_seeking_ = true;

    if (player_ != nullptr)
    {
        QMetaObject::invokeMethod(player_, "pause_feeding", Qt::QueuedConnection, Q_ARG(qint64, current_session_id_));
    }

    emit request_seek(current_session_id_, position_ms);
}

void mainwindow::on_seek_finished(qint64 session_id, qint64 actual_seek_ms)
{
    if (session_id != current_session_id_)
    {
        LOG_WARN("session {} ignoring seek_finished for obsolete session current is {}", session_id, current_session_id_);
        return;
    }

    if (actual_seek_ms < 0)
    {
        LOG_WARN("session {} seek failed resuming playback", session_id);
        is_seeking_ = false;
        pending_seek_ms_ = -1;
        if (is_playing_ && player_ != nullptr)
        {
            QMetaObject::invokeMethod(player_, "resume_feeding", Qt::QueuedConnection, Q_ARG(qint64, session_id));
        }
        on_progress_update(session_id, progress_slider_->value());
        return;
    }

    if (total_duration_ms_ > 0 && total_duration_ms_ - actual_seek_ms < 250)
    {
        LOG_INFO("session {} seek result is at the end, transitioning to finished state", session_id);
        is_seeking_ = false;

        if (player_ != nullptr)
        {
            QMetaObject::invokeMethod(player_, "handle_seek", Qt::QueuedConnection, Q_ARG(qint64, session_id), Q_ARG(qint64, actual_seek_ms));
        }

        on_playback_finished(session_id);
        return;
    }

    LOG_INFO("session {} seek finished at {}ms notifying player", session_id, actual_seek_ms);

    buffered_bytes_ = 0;
    decoder_is_waiting_ = false;

    if (player_ != nullptr)
    {
        spectrum_widget_->start_playback(actual_seek_ms);
        QMetaObject::invokeMethod(player_, "handle_seek", Qt::QueuedConnection, Q_ARG(qint64, session_id), Q_ARG(qint64, actual_seek_ms));
    }

    if (pending_seek_ms_ != -1)
    {
        LOG_INFO("session {} pending seek found to {}ms starting it now", session_id, pending_seek_ms_);
        qint64 new_seek_pos = pending_seek_ms_;
        pending_seek_ms_ = -1;
        emit request_seek(session_id, new_seek_pos);
        return;
    }

    is_seeking_ = false;
    is_playing_ = true;

    if (player_ != nullptr)
    {
        QMetaObject::invokeMethod(player_, "resume_feeding", Qt::QueuedConnection, Q_ARG(qint64, session_id));
        emit request_resume_decoding();
    }
}

void mainwindow::on_progress_slider_moved(int position)
{
    if (is_slider_pressed_)
    {
        time_label_->setText(QString("%1 / %2").arg(format_time(position)).arg(format_time(total_duration_ms_)));
    }
}

void mainwindow::create_new_playlist(const QString& name, bool is_loading, const QString& id)
{
    playlist data;
    data.id = id.isEmpty() ? QUuid::createUuid().toString() : id;
    data.name = name;

    data.button = new QPushButton(name);
    data.button->setCheckable(true);
    data.button->setAutoExclusive(false);
    data.button->setContextMenuPolicy(Qt::CustomContextMenu);
    data.button->setProperty("playlist_id", data.id);
    connect(data.button, &QPushButton::clicked, this, &mainwindow::on_playlist_button_clicked);
    connect(data.button, &QPushButton::customContextMenuRequested, this, &mainwindow::on_nav_button_context_menu_requested);

    data.widget = new QListWidget();
    data.widget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    data.widget->setContextMenuPolicy(Qt::CustomContextMenu);
    data.widget->setProperty("playlist_id", data.id);
    connect(data.widget, &QListWidget::itemDoubleClicked, this, &mainwindow::on_list_double_clicked);
    connect(data.widget, &QListWidget::customContextMenuRequested, this, &mainwindow::on_playlist_context_menu_requested);

    playlist_stack_->addWidget(data.widget);
    playlists_.insert(data.id, data);

    int insert_pos = playlist_nav_layout_->indexOf(nav_separator_);
    playlist_nav_layout_->insertWidget(insert_pos, data.button);

    if (!is_loading)
    {
        switch_to_playlist(data.id);
    }
}

void mainwindow::finish_playlist_edit()
{
    if (currently_editing_ == nullptr)
    {
        return;
    }

    QLineEdit* line_edit = currently_editing_;
    currently_editing_ = nullptr;

    QString new_name = line_edit->text().trimmed();

    playlist_nav_layout_->removeWidget(line_edit);
    line_edit->deleteLater();

    if (!new_name.isEmpty())
    {
        create_new_playlist(new_name, false);
    }
}

void mainwindow::add_new_playlist()
{
    if (currently_editing_ != nullptr)
    {
        finish_playlist_edit();
    }

    auto* lineEdit = new QLineEdit("新列表", this);
    lineEdit->selectAll();

    currently_editing_ = lineEdit;

    int insert_pos = playlist_nav_layout_->indexOf(nav_separator_);
    playlist_nav_layout_->insertWidget(insert_pos, lineEdit);

    connect(lineEdit, &QLineEdit::editingFinished, this, &mainwindow::finish_playlist_edit);

    lineEdit->setFocus();
}

void mainwindow::delete_playlist(const QString& id)
{
    if (!playlists_.contains(id))
    {
        return;
    }

    if (playlists_.count() <= 1)
    {
        QMessageBox::warning(this, "操作失败", "不能删除最后一个播放列表。");
        return;
    }

    if (is_playing_ && current_playlist_id_ == id)
    {
        stop_playback();
    }

    playlist data = playlists_.take(id);

    playlist_nav_layout_->removeWidget(data.button);
    playlist_stack_->removeWidget(data.widget);
    delete data.button;
    delete data.widget;

    if (current_playlist_id_ == id && !playlists_.isEmpty())
    {
        switch_to_playlist(playlists_.firstKey());
    }
}

void mainwindow::on_playlist_button_clicked()
{
    if (currently_editing_ != nullptr)
    {
        finish_playlist_edit();
    }
    auto* button = qobject_cast<QPushButton*>(sender());
    if (button != nullptr)
    {
        QString id = button->property("playlist_id").toString();
        switch_to_playlist(id);
    }
}

void mainwindow::switch_to_playlist(const QString& id)
{
    if (!playlists_.contains(id))
    {
        return;
    }

    current_playlist_id_ = id;
    playlist_stack_->setCurrentWidget(playlists_[id].widget);

    for (const auto& playlist : std::as_const(playlists_))
    {
        playlist.button->setChecked(playlist.id == id);
    }
}

void mainwindow::on_playlist_context_menu_requested(const QPoint& pos)
{
    if (currently_editing_ != nullptr)
    {
        finish_playlist_edit();
    }

    auto* current_list = current_song_list_widget();
    if (current_list == nullptr)
    {
        return;
    }

    QMenu context_menu;

    QAction* add_action = context_menu.addAction("添加音乐到当前列表...");
    connect(add_action,
            &QAction::triggered,
            [this, current_list]()
            {
                QStringList file_paths =
                    QFileDialog::getOpenFileNames(this, "选择音乐文件", "", "音频文件 (*.mp3 *.flac *.ogg *.wav *.mp4 *.mkv *.m4a *.webm)");
                for (const QString& path : file_paths)
                {
                    auto* item = new QListWidgetItem(QFileInfo(path).fileName());
                    item->setData(Qt::UserRole, path);
                    current_list->addItem(item);
                }
            });

    context_menu.addSeparator();

    QAction* delete_action = context_menu.addAction("从列表中删除");
    delete_action->setEnabled(!current_list->selectedItems().isEmpty());
    connect(delete_action,
            &QAction::triggered,
            [this, current_list]()
            {
                bool is_playing_item_deleted = is_media_loaded_ && current_list->selectedItems().contains(current_list->currentItem());
                for (QListWidgetItem* item : current_list->selectedItems())
                {
                    delete current_list->takeItem(current_list->row(item));
                }
                if (is_playing_item_deleted)
                {
                    stop_playback();
                }
            });

    context_menu.exec(current_list->mapToGlobal(pos));
}

void mainwindow::on_nav_button_context_menu_requested(const QPoint& pos)
{
    if (currently_editing_ != nullptr)
    {
        finish_playlist_edit();
    }

    auto* button = qobject_cast<QPushButton*>(sender());
    if (button == nullptr)
    {
        return;
    }

    QString id = button->property("playlist_id").toString();
    if (id.isEmpty())
    {
        return;
    }

    QMenu context_menu;
    QAction* delete_action = context_menu.addAction("删除播放列表 \"" + button->text() + "\"");
    connect(delete_action, &QAction::triggered, this, [this, id]() { delete_playlist(id); });

    context_menu.exec(button->mapToGlobal(pos));
}

void mainwindow::load_playlist()
{
    QFile playlist_file(playlist_path_);
    if (!playlist_file.exists() || !playlist_file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        LOG_INFO("playlists file not found creating a default list");
        create_new_playlist("默认列表", false);
        return;
    }

    LOG_INFO("loading playlists from {}", playlist_path_.toStdString());
    QTextStream in(&playlist_file);
    QString current_id;
    QString current_name;
    QList<QString> song_paths;

    auto finalize_current_playlist = [&]()
    {
        if (!current_id.isEmpty() && !current_name.isEmpty())
        {
            create_new_playlist(current_name, true, current_id);
            QListWidget* widget = get_list_widget_by_id(current_id);
            if (widget)
            {
                for (const QString& path : std::as_const(song_paths))
                {
                    QFileInfo file_info(path);
                    if (file_info.exists() && file_info.isFile())
                    {
                        auto* item = new QListWidgetItem(file_info.fileName());
                        item->setData(Qt::UserRole, path);
                        widget->addItem(item);
                    }
                    else
                    {
                        LOG_WARN("file from playlist not found skipping {}", path.toStdString());
                    }
                }
            }
        }
        current_id.clear();
        current_name.clear();
        song_paths.clear();
    };

    while (!in.atEnd())
    {
        QString line = in.readLine().trimmed();
        if (line.startsWith("[PLAYLIST_ID]"))
        {
            finalize_current_playlist();
            current_id = line.mid(13);
        }
        else if (line.startsWith("[PLAYLIST_NAME]"))
        {
            current_name = line.mid(15);
        }
        else if (line.startsWith("[PLAYLIST]"))
        {
            finalize_current_playlist();
            current_id = QUuid::createUuid().toString();
            current_name = line.mid(10);
        }
        else if (!line.isEmpty())
        {
            song_paths.append(line);
        }
    }
    finalize_current_playlist();

    if (playlists_.isEmpty())
    {
        create_new_playlist("默认列表", false);
    }
    else
    {
        switch_to_playlist(playlists_.firstKey());
    }
}

void mainwindow::save_playlist()
{
    QFile playlist_file(playlist_path_);
    if (!playlist_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        LOG_ERROR("open playlists file failed {}", playlist_path_.toStdString());
        return;
    }

    LOG_INFO("saving playlists to {}", playlist_path_.toStdString());
    QTextStream out(&playlist_file);
    for (const auto& id : playlists_.keys())
    {
        const playlist& data = playlists_[id];
        out << "[PLAYLIST_ID]" << data.id << "\n";
        out << "[PLAYLIST_NAME]" << data.name << "\n";
        if (data.widget != nullptr)
        {
            for (int j = 0; j < data.widget->count(); ++j)
            {
                out << data.widget->item(j)->data(Qt::UserRole).toString() << "\n";
            }
        }
    }
}

QListWidget* mainwindow::current_song_list_widget() const
{
    if (main_stack_widget_->currentWidget() != player_view_widget_ || current_playlist_id_.isEmpty())
    {
        return nullptr;
    }
    return get_list_widget_by_id(current_playlist_id_);
}

QListWidget* mainwindow::get_list_widget_by_id(const QString& id) const
{
    if (playlists_.contains(id))
    {
        return playlists_[id].widget;
    }
    return nullptr;
}

void mainwindow::show_management_view()
{
    populate_management_view();
    main_stack_widget_->setCurrentWidget(management_view_widget_);
}

void mainwindow::show_player_view() { main_stack_widget_->setCurrentWidget(player_view_widget_); }

void mainwindow::populate_management_view()
{
    mgmt_source_playlists_->clear();
    mgmt_dest_playlists_->clear();
    mgmt_source_songs_->clear();
    mgmt_dest_songs_->clear();

    for (const auto& id : playlists_.keys())
    {
        const playlist& data = playlists_[id];
        auto* source_item = new QListWidgetItem(data.name);
        source_item->setData(Qt::UserRole, data.id);
        mgmt_source_playlists_->addItem(source_item);

        auto* dest_item = new QListWidgetItem(data.name);
        dest_item->setData(Qt::UserRole, data.id);
        mgmt_dest_playlists_->addItem(dest_item);
    }
}

void mainwindow::update_management_source_songs(QListWidgetItem* item)
{
    mgmt_source_songs_->clear();
    if (item == nullptr)
    {
        return;
    }

    QString playlist_id = item->data(Qt::UserRole).toString();
    QListWidget* source_list = get_list_widget_by_id(playlist_id);
    if (source_list != nullptr)
    {
        for (int i = 0; i < source_list->count(); ++i)
        {
            QListWidgetItem* song_item = source_list->item(i);
            auto* new_item = new QListWidgetItem(song_item->text());
            new_item->setData(Qt::UserRole, song_item->data(Qt::UserRole));

            new_item->setFlags(new_item->flags() | Qt::ItemIsUserCheckable);
            new_item->setCheckState(Qt::Unchecked);

            mgmt_source_songs_->addItem(new_item);
        }
    }
}

void mainwindow::update_management_dest_songs(QListWidgetItem* item)
{
    mgmt_dest_songs_->clear();
    if (item == nullptr)
    {
        return;
    }

    QString playlist_id = item->data(Qt::UserRole).toString();
    QListWidget* dest_list = get_list_widget_by_id(playlist_id);
    if (dest_list != nullptr)
    {
        for (int i = 0; i < dest_list->count(); ++i)
        {
            QListWidgetItem* song_item = dest_list->item(i);
            auto* new_item = new QListWidgetItem(song_item->text());
            new_item->setData(Qt::UserRole, song_item->data(Qt::UserRole));
            mgmt_dest_songs_->addItem(new_item);
        }
    }
}

void mainwindow::add_selected_songs_to_playlist()
{
    QList<QListWidgetItem*> checked_songs;
    for (int i = 0; i < mgmt_source_songs_->count(); ++i)
    {
        QListWidgetItem* item = mgmt_source_songs_->item(i);
        if (item != nullptr && item->checkState() == Qt::Checked)
        {
            checked_songs.append(item);
        }
    }

    QListWidgetItem* dest_playlist_item = mgmt_dest_playlists_->currentItem();

    if (checked_songs.isEmpty() || dest_playlist_item == nullptr)
    {
        LOG_WARN("add songs failed: no songs checked or no destination playlist selected");
        return;
    }

    QString dest_playlist_id = dest_playlist_item->data(Qt::UserRole).toString();
    QListWidget* target_list_widget = get_list_widget_by_id(dest_playlist_id);

    if (target_list_widget == nullptr)
    {
        LOG_ERROR("could not find target playlist widget for id {}", dest_playlist_id.toStdString());
        return;
    }

    for (QListWidgetItem* song_item : std::as_const(checked_songs))
    {
        auto* new_item = new QListWidgetItem(song_item->text());
        new_item->setData(Qt::UserRole, song_item->data(Qt::UserRole));
        target_list_widget->addItem(new_item);
    }
    LOG_INFO("added {} songs to playlist {}", checked_songs.count(), dest_playlist_item->text().toStdString());

    for (QListWidgetItem* song_item : std::as_const(checked_songs))
    {
        song_item->setCheckState(Qt::Unchecked);
    }

    update_management_dest_songs(dest_playlist_item);
}
