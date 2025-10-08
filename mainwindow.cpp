#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QLabel>
#include <QListWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QFileDialog>
#include <QMediaDevices>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QInputDialog>
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

    setup_ui();
    setup_connections();
    init_audio_output();

    const QString app_data_path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(app_data_path);
    if (!dir.exists())
    {
        dir.mkpath(".");
    }
    playlist_path_ = app_data_path + "/playlists.txt";
    load_playlist();

    setWindowTitle("音乐播放器");
    resize(800, 600);
}

mainwindow::~mainwindow() { stop_playback(); }

void mainwindow::closeEvent(QCloseEvent* event)
{
    save_playlist();
    QMainWindow::closeEvent(event);
}

void mainwindow::keyPressEvent(QKeyEvent* event)
{
    QListWidget* current_list = current_playlist_widget();
    if (event->key() == Qt::Key_Delete && current_list != nullptr && current_list->hasFocus())
    {
        on_playlist_context_menu_requested(QPoint());
    }
    else
    {
        QMainWindow::keyPressEvent(event);
    }
}

void mainwindow::setup_ui()
{
    auto* central_widget = new QWidget;
    auto* main_layout = new QVBoxLayout(central_widget);
    spectrum_widget_ = new spectrum_widget;

    playlist_tabs_ = new QTabWidget();
    playlist_tabs_->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);

    auto* content_layout = new QHBoxLayout;
    content_layout->addWidget(spectrum_widget_, 2);
    content_layout->addWidget(playlist_tabs_, 1);

    auto* progress_layout = new QHBoxLayout;
    progress_slider_ = new QSlider(Qt::Horizontal);
    time_label_ = new QLabel("00:00 / 00:00");
    progress_layout->addWidget(progress_slider_);
    progress_layout->addWidget(time_label_);

    main_layout->addLayout(content_layout);
    main_layout->addLayout(progress_layout);
    setCentralWidget(central_widget);
}

void mainwindow::setup_connections()
{
    connect(playlist_tabs_->tabBar(), &QTabBar::customContextMenuRequested, this, &mainwindow::on_tab_bar_context_menu_requested);
    connect(decoder_thread_, &audio_decoder::decoding_finished, this, &mainwindow::on_decoding_finished, Qt::QueuedConnection);
    connect(decoder_thread_, &audio_decoder::duration_ready, this, &mainwindow::on_duration_ready, Qt::QueuedConnection);
    connect(progress_slider_, &QSlider::sliderMoved, this, &mainwindow::on_slider_moved);
}

void mainwindow::init_audio_output()
{
    auto format = default_audio_format();
    audio_sink_ = new QAudioSink(QMediaDevices::defaultAudioOutput(), format, this);
    audio_sink_->setBufferSize(default_audio_bytes_second() * kAudioBufferDurationSeconds);
}

void mainwindow::create_new_playlist_tab(const QString& name)
{
    auto* new_list_widget = new QListWidget();
    new_list_widget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    new_list_widget->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(new_list_widget, &QListWidget::itemDoubleClicked, this, &mainwindow::on_list_double_clicked);
    connect(new_list_widget, &QListWidget::customContextMenuRequested, this, &mainwindow::on_playlist_context_menu_requested);

    playlist_tabs_->addTab(new_list_widget, name);
    playlist_tabs_->setCurrentWidget(new_list_widget);
}

QListWidget* mainwindow::current_playlist_widget() const { return qobject_cast<QListWidget*>(playlist_tabs_->currentWidget()); }

void mainwindow::add_new_playlist()
{
    QInputDialog dialog(this);

    dialog.setWindowTitle("新建播放列表");
    dialog.setLabelText("列表名称:");
    dialog.setTextValue("新列表");
    dialog.setInputMode(QInputDialog::TextInput);

    dialog.resize(350, 120);

    if (dialog.exec() == QDialog::Accepted)
    {
        QString name = dialog.textValue();
        if (!name.isEmpty())
        {
            create_new_playlist_tab(name);
        }
    }
}

void mainwindow::delete_playlist(int index)
{
    if (playlist_tabs_->count() <= 1)
    {
        QMessageBox::warning(this, "操作失败", "不能删除最后一个播放列表。");
        return;
    }

    auto* list_to_delete = qobject_cast<QListWidget*>(playlist_tabs_->widget(index));
    QListWidget* current_playing_list = is_playing_ ? qobject_cast<QListWidget*>(playlist_tabs_->currentWidget()) : nullptr;

    if (is_playing_ && list_to_delete == current_playing_list)
    {
        stop_playback();
    }

    playlist_tabs_->removeTab(index);
}

void mainwindow::on_list_double_clicked(QListWidgetItem* item)
{
    if (item == nullptr)
    {
        return;
    }

    playlist_tabs_->setCurrentWidget(item->listWidget());

    LOG_DEBUG("start playback");
    stop_playback();

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

    QListWidget* current_list = current_playlist_widget();
    if (current_list != nullptr)
    {
        current_list->setCurrentItem(item);
    }

    QTimer::singleShot(50, this, &mainwindow::feed_audio_device);
}

void mainwindow::on_playlist_context_menu_requested(const QPoint& pos)
{
    QListWidget* current_list = current_playlist_widget();
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

    QAction* new_list_action = context_menu.addAction("新建播放列表...");
    connect(new_list_action, &QAction::triggered, this, &mainwindow::add_new_playlist);

    context_menu.addSeparator();

    QAction* stop_action = context_menu.addAction("停止");
    stop_action->setEnabled(is_playing_);
    connect(stop_action, &QAction::triggered, this, &mainwindow::stop_playback);

    QAction* delete_action = context_menu.addAction("从列表中删除");
    delete_action->setEnabled(!current_list->selectedItems().isEmpty());
    connect(delete_action,
            &QAction::triggered,
            [this, current_list]()
            {
                bool is_current_playing_item_deleted = false;
                if (is_playing_)
                {
                    if (current_list->selectedItems().contains(current_list->currentItem()))
                    {
                        is_current_playing_item_deleted = true;
                    }
                }
                for (QListWidgetItem* item : current_list->selectedItems())
                {
                    delete current_list->takeItem(current_list->row(item));
                }
                if (is_current_playing_item_deleted)
                {
                    stop_playback();
                }
            });

    context_menu.exec(current_list->mapToGlobal(pos));
}

void mainwindow::on_tab_bar_context_menu_requested(const QPoint& pos)
{
    int tab_index = playlist_tabs_->tabBar()->tabAt(pos);
    if (tab_index < 0)
    {
        return;
    }

    QMenu context_menu;
    QAction* delete_action = context_menu.addAction("删除播放列表 \"" + playlist_tabs_->tabText(tab_index) + "\"");
    connect(delete_action, &QAction::triggered, this, [this, tab_index]() { delete_playlist(tab_index); });

    context_menu.exec(playlist_tabs_->tabBar()->mapToGlobal(pos));
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

void mainwindow::on_slider_moved(int position) { update_progress(position); }

void mainwindow::update_progress(qint64 position_ms)
{
    if (!progress_slider_->isSliderDown())
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
        LOG_INFO("Playlists file not found. Creating a default list.");
        create_new_playlist_tab("默认列表");
        return;
    }

    LOG_INFO("Loading playlists from {}", playlist_path_.toStdString());
    QTextStream in(&playlist_file);
    QListWidget* current_list_for_loading = nullptr;

    while (!in.atEnd())
    {
        QString line = in.readLine().trimmed();
        if (line.startsWith("[PLAYLIST]"))
        {
            QString name = line.mid(10);
            create_new_playlist_tab(name);
            current_list_for_loading = current_playlist_widget();
        }
        else if (current_list_for_loading != nullptr && !line.isEmpty())
        {
            QFileInfo file_info(line);
            if (file_info.exists() && file_info.isFile())
            {
                auto* item = new QListWidgetItem(file_info.fileName());
                item->setData(Qt::UserRole, line);
                current_list_for_loading->addItem(item);
            }
            else
            {
                LOG_WARN("File from playlist not found, skipping: {}", line.toStdString());
            }
        }
    }

    if (playlist_tabs_->count() == 0)
    {
        create_new_playlist_tab("默认列表");
    }
}

void mainwindow::save_playlist()
{
    QFile playlist_file(playlist_path_);
    if (!playlist_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        LOG_ERROR("Could not open playlists file for writing: {}", playlist_path_.toStdString());
        return;
    }

    LOG_INFO("Saving playlists to {}", playlist_path_.toStdString());
    QTextStream out(&playlist_file);

    for (int i = 0; i < playlist_tabs_->count(); ++i)
    {
        out << "[PLAYLIST]" << playlist_tabs_->tabText(i) << "\n";

        auto* list = qobject_cast<QListWidget*>(playlist_tabs_->widget(i));
        if (list != nullptr)
        {
            for (int j = 0; j < list->count(); ++j)
            {
                out << list->item(j)->data(Qt::UserRole).toString() << "\n";
            }
        }
    }
}
