#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QStackedWidget>
#include <QButtonGroup>
#include <QSlider>
#include <QLabel>
#include <QListWidget>
#include <QSplitter>
#include <QLineEdit>
#include <QMouseEvent>
#include <QApplication>
#include <QFileDialog>
#include <QMediaDevices>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QThread>

#include "log.h"
#include "mainwindow.h"
#include "audio_decoder.h"
#include "audio_player.h"
#include "spectrum_widget.h"

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

    playlist_button_group_ = new QButtonGroup(this);

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
    auto* nav_container = new QWidget();
    nav_container->setObjectName("navContainer");
    playlist_nav_layout_ = new QVBoxLayout(nav_container);
    playlist_nav_layout_->setSpacing(5);

    add_playlist_button_ = new QPushButton("+ 新建播放列表");
    add_playlist_button_->setObjectName("addButton");
    playlist_nav_layout_->addWidget(add_playlist_button_);
    playlist_nav_layout_->addStretch();

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

    auto* main_splitter = new QSplitter(Qt::Horizontal);
    main_splitter->addWidget(nav_container);
    main_splitter->addWidget(playlist_stack_);
    main_splitter->addWidget(right_column_widget);
    main_splitter->setSizes(QList<int>() << 200 << 300 << 500);
    main_splitter->setStretchFactor(1, 1);
    main_splitter->setStretchFactor(2, 2);
    setCentralWidget(main_splitter);
}

void mainwindow::setup_connections()
{
    connect(add_playlist_button_, &QPushButton::clicked, this, &mainwindow::add_new_playlist);
    connect(playlist_button_group_, QOverload<int>::of(&QButtonGroup::idClicked), this, &mainwindow::on_playlist_button_clicked);
    connect(progress_slider_, &QSlider::sliderMoved, this, &mainwindow::on_progress_slider_moved);
    connect(progress_slider_, &QSlider::sliderPressed, this, [this] { is_slider_pressed_ = true; });
    connect(progress_slider_, &QSlider::sliderReleased, this, &mainwindow::on_seek_requested);

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
    if (list != nullptr)
    {
        list->setCurrentItem(item);
        int index = playlist_stack_->indexOf(list);
        if (index != -1)
        {
            playlist_button_group_->button(index)->setChecked(true);
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
    buffer_high_water_mark_ = 5L * format.bytesPerFrame() * format.sampleRate();
    LOG_INFO("session {} buffer high water mark set to {} bytes 5 seconds", session_id, buffer_high_water_mark_);

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

    // 音乐播放结束，不清理资源，只更新状态
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

    // 暂停播放，准备接收新的 seek 位置
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
        // 如果 seek 失败，只有在之前是播放状态时才恢复喂食
        if (is_playing_ && player_ != nullptr)
        {
            QMetaObject::invokeMethod(player_, "resume_feeding", Qt::QueuedConnection, Q_ARG(qint64, session_id));
        }
        on_progress_update(session_id, progress_slider_->value());
        return;
    }

    // 如果 seek 的实际结果非常接近文件末尾，直接转换到播放结束状态
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

void mainwindow::create_new_playlist(const QString& name, bool is_loading)
{
    auto* nav_button = new QPushButton(name);
    nav_button->setCheckable(true);
    nav_button->setAutoExclusive(true);
    nav_button->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(nav_button, &QPushButton::customContextMenuRequested, this, &mainwindow::on_nav_button_context_menu_requested);

    auto* list_widget = new QListWidget();
    list_widget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_widget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list_widget, &QListWidget::itemDoubleClicked, this, &mainwindow::on_list_double_clicked);
    connect(list_widget, &QListWidget::customContextMenuRequested, this, &mainwindow::on_playlist_context_menu_requested);

    int new_id = playlist_stack_->addWidget(list_widget);
    playlist_button_group_->addButton(nav_button, new_id);

    int insert_pos = playlist_nav_layout_->indexOf(add_playlist_button_);
    playlist_nav_layout_->insertWidget(insert_pos, nav_button);

    if (!is_loading)
    {
        nav_button->setChecked(true);
        on_playlist_button_clicked(new_id);
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

    int insert_pos = playlist_nav_layout_->indexOf(add_playlist_button_);
    playlist_nav_layout_->insertWidget(insert_pos, lineEdit);

    connect(lineEdit, &QLineEdit::editingFinished, this, &mainwindow::finish_playlist_edit);

    lineEdit->setFocus();
}

void mainwindow::delete_playlist(int index)
{
    if (index < 0 || index >= playlist_stack_->count())
    {
        return;
    }

    if (playlist_stack_->count() <= 1)
    {
        QMessageBox::warning(this, "操作失败", "不能删除最后一个播放列表。");
        return;
    }

    if (is_playing_ && playlist_stack_->currentIndex() == index)
    {
        stop_playback();
    }

    auto* button = qobject_cast<QPushButton*>(playlist_button_group_->button(index));
    QWidget* list_widget = playlist_stack_->widget(index);

    if (button != nullptr && list_widget != nullptr)
    {
        bool is_current = button->isChecked();

        playlist_button_group_->removeButton(button);
        playlist_nav_layout_->removeWidget(button);
        playlist_stack_->removeWidget(list_widget);
        delete button;
        delete list_widget;

        if (is_current && playlist_button_group_->buttons().size() > 0)
        {
            int first_id = playlist_button_group_->id(playlist_button_group_->buttons().first());
            playlist_button_group_->button(first_id)->setChecked(true);
            on_playlist_button_clicked(first_id);
        }
    }
}

void mainwindow::on_playlist_button_clicked(int id)
{
    if (currently_editing_ != nullptr)
    {
        finish_playlist_edit();
    }
    playlist_stack_->setCurrentIndex(id);
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
                QStringList file_paths = QFileDialog::getOpenFileNames(this, "选择音乐文件", "", "音频文件 (*.mp3 *.flac *.ogg *.wav)");
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

    int index = playlist_button_group_->id(button);
    if (index < 0)
    {
        return;
    }

    QMenu context_menu;
    QAction* delete_action = context_menu.addAction("删除播放列表 \"" + button->text() + "\"");
    connect(delete_action, &QAction::triggered, this, [this, index]() { delete_playlist(index); });

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
    QListWidget* current_song_list_for_loading = nullptr;
    while (!in.atEnd())
    {
        QString line = in.readLine().trimmed();
        if (line.startsWith("[PLAYLIST]"))
        {
            QString name = line.mid(10);
            create_new_playlist(name, true);
            current_song_list_for_loading = qobject_cast<QListWidget*>(playlist_stack_->widget(playlist_stack_->count() - 1));
        }
        else if (current_song_list_for_loading != nullptr && !line.isEmpty())
        {
            QFileInfo file_info(line);
            if (file_info.exists() && file_info.isFile())
            {
                auto* item = new QListWidgetItem(file_info.fileName());
                item->setData(Qt::UserRole, line);
                current_song_list_for_loading->addItem(item);
            }
            else
            {
                LOG_WARN("file from playlist not found skipping {}", line.toStdString());
            }
        }
    }
    if (playlist_stack_->count() == 0)
    {
        create_new_playlist("默认列表", false);
    }
    else
    {
        playlist_button_group_->button(0)->setChecked(true);
        on_playlist_button_clicked(0);
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
    for (int i = 0; i < playlist_button_group_->buttons().count(); ++i)
    {
        auto* button = qobject_cast<QPushButton*>(playlist_button_group_->button(i));
        if (button == nullptr)
        {
            continue;
        }
        int id = playlist_button_group_->id(button);
        out << "[PLAYLIST]" << button->text() << "\n";
        QListWidget* list = get_list_widget_by_index(id);
        if (list != nullptr)
        {
            for (int j = 0; j < list->count(); ++j)
            {
                out << list->item(j)->data(Qt::UserRole).toString() << "\n";
            }
        }
    }
}

QListWidget* mainwindow::current_song_list_widget() const { return qobject_cast<QListWidget*>(playlist_stack_->currentWidget()); }

QListWidget* mainwindow::get_list_widget_by_index(int index) const { return qobject_cast<QListWidget*>(playlist_stack_->widget(index)); }
