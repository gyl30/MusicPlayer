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

#include "log.h"
#include "mainwindow.h"
#include "spectrum_widget.h"
#include "audio_decoder_thread.h"

constexpr auto kAudioBufferDurationSeconds = 2L;

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

static int default_audio_bytes_second()
{
    auto format = default_audio_format();
    return format.bytesPerFrame() * format.sampleRate();
}

mainwindow::mainwindow(QWidget* parent) : QMainWindow(parent)
{
    decoder_thread_ = new audio_decoder(this);
    auto pcm_handler = [this](const uint8_t* data, size_t size, int64_t timestamp_ms)
    {
        auto packet = std::make_shared<audio_packet>();
        packet->data.assign(data, data + size);
        packet->ms = timestamp_ms;
        data_queue_.enqueue(packet);
    };
    decoder_thread_->set_data_callback(pcm_handler);

    playlist_button_group_ = new QButtonGroup(this);

    setup_ui();
    setup_connections();
    init_audio_output();

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

    connect(decoder_thread_, &audio_decoder::decoding_finished, this, &mainwindow::on_decoding_finished, Qt::QueuedConnection);
    connect(decoder_thread_, &audio_decoder::duration_ready, this, &mainwindow::on_duration_ready, Qt::QueuedConnection);

    connect(progress_slider_, &QSlider::sliderMoved, this, &mainwindow::on_progress_slider_moved);
    connect(progress_slider_, &QSlider::sliderPressed, this, [this] { is_slider_pressed_ = true; });
    connect(progress_slider_, &QSlider::sliderReleased, this, &mainwindow::on_seek_requested);
}

void mainwindow::init_audio_output()
{
    auto format = default_audio_format();
    audio_sink_ = new QAudioSink(QMediaDevices::defaultAudioOutput(), format, this);
    audio_sink_->setBufferSize(default_audio_bytes_second() * kAudioBufferDurationSeconds);
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

    LOG_DEBUG("start playback");
    stop_playback();

    current_playing_file_path_ = item->data(Qt::UserRole).toString();

    io_device_ = audio_sink_->start();
    if (io_device_ == nullptr)
    {
        LOG_ERROR("启动音频设备的IODevice失败。");
        return;
    }
    is_playing_ = true;
    decoder_finished_ = false;

    spectrum_widget_->start_playback();
    decoder_thread_->start_decoding(item->data(Qt::UserRole).toString(), default_audio_format());

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

    QTimer::singleShot(50, this, &mainwindow::feed_audio_device);
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
                bool is_playing_item_deleted = is_playing_ && current_list->selectedItems().contains(current_list->currentItem());
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

void mainwindow::stop_playback()
{
    if (!is_playing_)
    {
        return;
    }
    LOG_DEBUG("stop playback");
    is_playing_ = false;
    decoder_thread_->stop();
    data_queue_.clear();
    spectrum_widget_->stop_playback();
    if (audio_sink_ != nullptr)
    {
        audio_sink_->stop();
    }
    io_device_ = nullptr;
    update_progress(0);
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

void mainwindow::feed_audio_device()
{
    if (!is_playing_)
    {
        return;
    }
    std::shared_ptr<audio_packet> last_packet = nullptr;
    while (audio_sink_->bytesFree() > 0 && !data_queue_.is_empty())
    {
        auto packet = data_queue_.try_dequeue();
        if (packet == nullptr)
        {
            break;
        }
        io_device_->write(reinterpret_cast<const char*>(packet->data.data()), static_cast<qint64>(packet->data.size()));
        spectrum_widget_->enqueue_packet(packet);
        last_packet = packet;
    }
    if (data_queue_.is_empty() && decoder_finished_)
    {
        const int bytes_per_second = default_audio_bytes_second();
        const qint64 bytes_buffered = audio_sink_->bufferSize() - audio_sink_->bytesFree();
        if (bytes_buffered > 0)
        {
            const qint64 buffered_duration_ms = (bytes_buffered * 1000) / bytes_per_second;
            QTimer::singleShot(buffered_duration_ms + 100, this, &mainwindow::stop_playback);
        }
        else
        {
            stop_playback();
        }
        return;
    }
    if (last_packet != nullptr)
    {
        update_progress(last_packet->ms);
    }
    if (is_playing_)
    {
        const int bytes_per_second = default_audio_bytes_second();
        const qint64 bytes_buffered = audio_sink_->bufferSize() - audio_sink_->bytesFree();
        const qint64 buffered_duration_ms = (bytes_buffered * 1000) / bytes_per_second;
        auto next_delay_ms = qBound(10, buffered_duration_ms / 2, 1000);
        QTimer::singleShot(next_delay_ms, this, &mainwindow::feed_audio_device);
    }
}

void mainwindow::on_progress_slider_moved(int position)
{
    if (is_slider_pressed_)
    {
        time_label_->setText(QString("%1 / %2").arg(format_time(position)).arg(format_time(total_duration_ms_)));
    }
}

void mainwindow::on_seek_requested()
{
    is_slider_pressed_ = false;
    qint64 position_ms = progress_slider_->value();

    if (!is_playing_ && !decoder_finished_)
    {
        return;
    }

    if (decoder_finished_)
    {
        if (current_playing_file_path_.isEmpty())
        {
            return;
        }

        LOG_DEBUG("Seek after finished, restarting playback at {} ms", position_ms);

        stop_playback();

        io_device_ = audio_sink_->start();
        if (io_device_ == nullptr)
        {
            LOG_ERROR("启动音频设备的IODevice失败。");
            return;
        }

        is_playing_ = true;
        decoder_finished_ = false;

        spectrum_widget_->start_playback(position_ms);
        decoder_thread_->start_decoding(current_playing_file_path_, default_audio_format(), position_ms);

        QTimer::singleShot(50, this, &mainwindow::feed_audio_device);
    }
    else
    {
        LOG_DEBUG("Seek requested to {} ms", position_ms);
        data_queue_.clear();
        spectrum_widget_->start_playback(position_ms);
        decoder_thread_->seek(position_ms);
    }
}

void mainwindow::update_progress(qint64 position_ms)
{
    if (!is_slider_pressed_)
    {
        progress_slider_->setValue(static_cast<int>(position_ms));
    }
    time_label_->setText(QString("%1 / %2").arg(format_time(position_ms)).arg(format_time(total_duration_ms_)));
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
