#include <QApplication>
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
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QDebug>
#include <QPalette>

#include "log.h"
#include "mainwindow.h"
#include "spectrum_widget.h"
#include "playlist_manager.h"
#include "playback_controller.h"

static QString format_time(qint64 time_ms)
{
    int total_seconds = static_cast<int>(time_ms) / 1000;
    int minutes = total_seconds / 60;
    int seconds = total_seconds % 60;
    return QString("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
}

mainwindow::mainwindow(QWidget* parent) : QMainWindow(parent)
{
    playlist_manager_ = new playlist_manager(this);
    controller_ = new playback_controller(this);

    setup_ui();
    setup_connections();

    controller_->set_spectrum_widget(spectrum_widget_);

    qApp->installEventFilter(this);

    playlist_manager_->load_playlists();

    setWindowTitle("音乐播放器");
    resize(1000, 700);
}

mainwindow::~mainwindow() { qApp->removeEventFilter(this); }

void mainwindow::closeEvent(QCloseEvent* event)
{
    playlist_manager_->save_playlists();
    QMainWindow::closeEvent(event);
}

void mainwindow::keyPressEvent(QKeyEvent* event)
{
    QListWidget* current_list = current_song_list_widget();
    if (event->key() == Qt::Key_Delete && current_list != nullptr && current_list->hasFocus())
    {
        on_remove_songs_requested();
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
    mgmt_source_songs_->setSelectionMode(QAbstractItemView::ExtendedSelection);
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

    status_bar_ = new QStatusBar();
    setStatusBar(status_bar_);

    LOG_INFO("主窗口：为状态栏创建永久部件");
    file_path_label_ = new QLabel(this);
    status_bar_->addPermanentWidget(file_path_label_);

    LOG_INFO("主窗口：UI设置完成");
}

void mainwindow::setup_connections()
{
    connect(add_playlist_button_, &QPushButton::clicked, this, &mainwindow::on_add_new_playlist_button_clicked);
    connect(management_button_, &QPushButton::clicked, this, &mainwindow::show_management_view);

    connect(progress_slider_, &QSlider::sliderMoved, this, &mainwindow::on_progress_slider_moved);
    connect(progress_slider_, &QSlider::sliderPressed, this, [this] { is_slider_pressed_ = true; });
    connect(progress_slider_, &QSlider::sliderReleased, this, &mainwindow::on_seek_requested);

    connect(controller_, &playback_controller::track_info_ready, this, &mainwindow::update_track_info);
    connect(controller_, &playback_controller::progress_updated, this, &mainwindow::update_progress);
    connect(controller_, &playback_controller::playback_finished, this, &mainwindow::handle_playback_finished);
    connect(controller_, &playback_controller::playback_error, this, &mainwindow::handle_playback_error);
    connect(controller_, &playback_controller::seek_finished, this, &mainwindow::handle_seek_finished);
    connect(controller_, &playback_controller::seek_completed, this, &mainwindow::on_seek_completed);

    LOG_INFO("主窗口：正在连接 playback_started 信号");
    connect(controller_, &playback_controller::playback_started, this, &mainwindow::on_playback_started);

    connect(playlist_manager_, &playlist_manager::playlists_changed, this, &mainwindow::rebuild_ui_from_playlists);
    connect(playlist_manager_, &playlist_manager::playlist_content_changed, this, &mainwindow::update_playlist_content);

    connect(finish_management_button_, &QPushButton::clicked, this, &mainwindow::show_player_view);
    connect(add_songs_to_playlist_button_, &QPushButton::clicked, this, &mainwindow::add_selected_songs_to_playlist);
    connect(mgmt_source_playlists_, &QListWidget::currentItemChanged, this, &mainwindow::update_management_source_songs);
    connect(mgmt_dest_playlists_, &QListWidget::currentItemChanged, this, &mainwindow::update_management_dest_songs);
    LOG_INFO("主窗口：信号槽连接设置完成");
}

void mainwindow::on_play_file_requested(QListWidgetItem* item)
{
    if (item == nullptr)
    {
        return;
    }
    LOG_INFO("播放流程 1/14 UI层接收到播放请求 转发至控制中心");
    current_playing_file_path_ = item->data(Qt::UserRole).toString();
    controller_->play_file(current_playing_file_path_);
}

void mainwindow::on_seek_requested()
{
    is_slider_pressed_ = false;
    qint64 position_ms = progress_slider_->value();
    LOG_INFO("跳转流程 1/10 UI层请求跳转至 {}ms 转发至控制中心", position_ms);
    controller_->seek(position_ms);
}

void mainwindow::on_progress_slider_moved(int position)
{
    if (is_slider_pressed_)
    {
        time_label_->setText(QString("%1 / %2").arg(format_time(position)).arg(format_time(total_duration_ms_)));
    }
}

void mainwindow::update_track_info(qint64 duration_ms)
{
    LOG_INFO("播放流程 7/14 UI层收到音轨信息 更新时长 {}ms", duration_ms);
    total_duration_ms_ = duration_ms;
    progress_slider_->setRange(0, static_cast<int>(total_duration_ms_));
    progress_slider_->setValue(0);
    time_label_->setText(QString("00:00 / %1").arg(format_time(total_duration_ms_)));
}

void mainwindow::update_progress(qint64 current_ms, qint64 total_ms)
{
    if (!is_slider_pressed_)
    {
        progress_slider_->setValue(static_cast<int>(current_ms));
    }
    time_label_->setText(QString("%1 / %2").arg(format_time(current_ms)).arg(format_time(total_ms)));
}

void mainwindow::handle_playback_finished()
{
    LOG_INFO("主窗口：处理播放结束，重置UI");
    clear_playing_indicator();
    setWindowTitle("音乐播放器");
    file_path_label_->clear();
    if (total_duration_ms_ > 0)
    {
        progress_slider_->setValue(static_cast<int>(total_duration_ms_));
        time_label_->setText(QString("%1 / %2").arg(format_time(total_duration_ms_)).arg(format_time(total_duration_ms_)));
    }
}

void mainwindow::handle_playback_error(const QString& error_message)
{
    QMessageBox::critical(this, "播放错误", error_message);
    clear_playing_indicator();
    setWindowTitle("音乐播放器");
    file_path_label_->clear();
    progress_slider_->setValue(0);
    time_label_->setText("00:00 / 00:00");
}

void mainwindow::handle_seek_finished(bool success)
{
    if (!success)
    {
        status_bar_->showMessage("跳转失败", 3000);
    }
}

void mainwindow::on_playback_started(const QString& file_path, const QString& file_name)
{
    LOG_INFO("主窗口：收到播放开始信号，为路径 {} 更新UI", file_path.toStdString());
    clear_playing_indicator();

    bool item_found = false;
    for (QListWidget* list_widget : std::as_const(playlist_widgets_))
    {
        for (int i = 0; i < list_widget->count(); ++i)
        {
            QListWidgetItem* item = list_widget->item(i);
            if (item->data(Qt::UserRole).toString() == file_path)
            {
                LOG_INFO("在播放列表中找到匹配项，应用播放指示器");
                currently_playing_item_ = item;

                QFont font = item->font();
                font.setBold(true);
                item->setFont(font);
                item->setText(QString("🔊 %1").arg(file_name));

                if (!item->isSelected())
                {
                    item->setBackground(palette().alternateBase());
                }

                item_found = true;
                break;
            }
        }
        if (item_found)
        {
            break;
        }
    }

    if (!item_found)
    {
        LOG_WARN("无法为路径 {} 找到列表项，无法设置指示器", file_path.toStdString());
    }

    LOG_INFO("正在更新窗口标题和状态栏");
    setWindowTitle(QString("正在播放: %1").arg(file_name));
    file_path_label_->setText(file_path);
}

void mainwindow::on_current_song_selection_changed(QListWidgetItem* current, QListWidgetItem* previous)
{
    LOG_INFO("主窗口：选择项变更，处理播放中项目的样式");
    if (previous != nullptr && previous == currently_playing_item_)
    {
        LOG_INFO("播放中项目失去焦点，恢复交替背景色");
        previous->setBackground(palette().alternateBase());
    }

    if (current != nullptr && current == currently_playing_item_)
    {
        LOG_INFO("播放中项目获得焦点，由系统处理高亮颜色");
    }
}

void mainwindow::on_seek_completed(qint64 actual_ms)
{
    LOG_INFO("UI界面更新至实际跳转位置: {}ms", actual_ms);
    progress_slider_->setValue(static_cast<int>(actual_ms));
    time_label_->setText(QString("%1 / %2").arg(format_time(actual_ms)).arg(format_time(total_duration_ms_)));
}

void mainwindow::clear_playing_indicator()
{
    if (currently_playing_item_ != nullptr)
    {
        LOG_INFO("正在清除上一个项目的播放指示器");

        QFont font = currently_playing_item_->font();
        font.setBold(false);
        currently_playing_item_->setFont(font);

        QString file_path = currently_playing_item_->data(Qt::UserRole).toString();
        QFileInfo file_info(file_path);
        currently_playing_item_->setText(file_info.fileName());

        currently_playing_item_->setBackground(QBrush());

        currently_playing_item_ = nullptr;
    }
}

void mainwindow::clear_playlist_ui()
{
    for (QPushButton* button : std::as_const(playlist_buttons_))
    {
        playlist_nav_layout_->removeWidget(button);
        button->deleteLater();
    }
    playlist_buttons_.clear();

    for (QListWidget* widget : std::as_const(playlist_widgets_))
    {
        playlist_stack_->removeWidget(widget);
        widget->deleteLater();
    }
    playlist_widgets_.clear();
}

void mainwindow::add_playlist_to_ui(const Playlist& playlist)
{
    auto* button = new QPushButton(playlist.name);
    button->setCheckable(true);
    button->setAutoExclusive(false);
    button->setContextMenuPolicy(Qt::CustomContextMenu);
    button->setProperty("playlist_id", playlist.id);
    connect(button, &QPushButton::clicked, this, &mainwindow::on_playlist_button_clicked);
    connect(button, &QPushButton::customContextMenuRequested, this, &mainwindow::on_nav_button_context_menu_requested);

    auto* widget = new QListWidget();
    widget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    widget->setContextMenuPolicy(Qt::CustomContextMenu);
    widget->setProperty("playlist_id", playlist.id);
    connect(widget, &QListWidget::itemDoubleClicked, this, &mainwindow::on_play_file_requested);
    connect(widget, &QListWidget::customContextMenuRequested, this, &mainwindow::on_playlist_context_menu_requested);

    LOG_INFO("正在为播放列表 {} 连接 currentItemChanged 信号", playlist.id.toStdString());
    connect(widget, &QListWidget::currentItemChanged, this, &mainwindow::on_current_song_selection_changed);

    for (const auto& song : playlist.songs)
    {
        auto* item = new QListWidgetItem(song.fileName);
        item->setData(Qt::UserRole, song.filePath);
        widget->addItem(item);
    }

    playlist_stack_->addWidget(widget);
    playlist_buttons_.insert(playlist.id, button);
    playlist_widgets_.insert(playlist.id, widget);

    int insert_pos = playlist_nav_layout_->indexOf(nav_separator_);
    playlist_nav_layout_->insertWidget(insert_pos, button);
}

void mainwindow::rebuild_ui_from_playlists()
{
    LOG_INFO("正在重建整个播放列表UI");
    clear_playlist_ui();

    QList<Playlist> all_playlists = playlist_manager_->get_all_playlists();
    for (const auto& playlist : std::as_const(all_playlists))
    {
        add_playlist_to_ui(playlist);
    }

    if (!current_playlist_id_.isEmpty() && playlist_widgets_.contains(current_playlist_id_))
    {
        switch_to_playlist(current_playlist_id_);
    }
    else if (!all_playlists.isEmpty())
    {
        switch_to_playlist(all_playlists.first().id);
    }
}

void mainwindow::update_playlist_content(const QString& playlist_id)
{
    LOG_INFO("正在更新播放列表ID {} 的内容", playlist_id.toStdString());
    if (!playlist_widgets_.contains(playlist_id))
    {
        return;
    }
    QListWidget* list_widget = playlist_widgets_[playlist_id];
    list_widget->clear();

    Playlist playlist_data = playlist_manager_->get_playlist_by_id(playlist_id);
    for (const auto& song : playlist_data.songs)
    {
        auto* item = new QListWidgetItem(song.fileName);
        item->setData(Qt::UserRole, song.filePath);
        list_widget->addItem(item);
    }
}

void mainwindow::switch_to_playlist(const QString& id)
{
    if (!playlist_widgets_.contains(id))
    {
        return;
    }
    LOG_INFO("正在切换到播放列表ID {}", id.toStdString());
    current_playlist_id_ = id;
    playlist_stack_->setCurrentWidget(playlist_widgets_[id]);

    for (const auto& button_id : playlist_buttons_.keys())
    {
        playlist_buttons_[button_id]->setChecked(button_id == id);
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

void mainwindow::finish_playlist_edit()
{
    if (currently_editing_ == nullptr)
    {
        return;
    }

    QLineEdit* line_edit = currently_editing_;
    currently_editing_ = nullptr;

    QString new_name = line_edit->text().trimmed();
    QString id_to_rename = line_edit->property("playlist_id").toString();

    playlist_nav_layout_->removeWidget(line_edit);
    line_edit->deleteLater();

    if (!new_name.isEmpty())
    {
        if (id_to_rename.isEmpty())
        {
            playlist_manager_->create_new_playlist(new_name);
        }
        else
        {
            playlist_manager_->rename_playlist(id_to_rename, new_name);
        }
    }
}

void mainwindow::on_add_new_playlist_button_clicked()
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

void mainwindow::on_delete_playlist_requested()
{
    auto* button = qobject_cast<QPushButton*>(sender()->parent());
    if (button == nullptr)
    {
        return;
    }
    QString id = button->property("playlist_id").toString();
    Playlist playlist = playlist_manager_->get_playlist_by_id(id);
    if (playlist.id.isEmpty())
    {
        return;
    }

    if (playlist_manager_->get_all_playlists().count() <= 1)
    {
        QMessageBox::warning(this, "操作失败", "不能删除最后一个播放列表。");
        return;
    }

    if (current_playing_file_path_.contains(playlist.id))
    {
        controller_->stop();
    }
    playlist_manager_->delete_playlist(id);
}

void mainwindow::on_rename_playlist_requested()
{
    if (currently_editing_ != nullptr)
    {
        finish_playlist_edit();
    }
    auto* button = qobject_cast<QPushButton*>(sender()->parent());
    if (button == nullptr)
    {
        return;
    }
    QString id = button->property("playlist_id").toString();
    int button_index = playlist_nav_layout_->indexOf(button);
    playlist_nav_layout_->removeWidget(button);

    auto* lineEdit = new QLineEdit(button->text(), this);
    lineEdit->setProperty("playlist_id", id);
    lineEdit->selectAll();
    currently_editing_ = lineEdit;
    playlist_nav_layout_->insertWidget(button_index, lineEdit);
    connect(lineEdit, &QLineEdit::editingFinished, this, &mainwindow::finish_playlist_edit);
    lineEdit->setFocus();
}

void mainwindow::on_add_songs_requested()
{
    QStringList file_paths =
        QFileDialog::getOpenFileNames(this, "选择音乐文件", "", "音频文件 (*.mp3 *.flac *.ogg *.ape *.wav *.mp4 *.mkv *.m4a *.webm)");
    if (!file_paths.isEmpty())
    {
        playlist_manager_->add_songs_to_playlist(current_playlist_id_, file_paths);
    }
}

void mainwindow::on_remove_songs_requested()
{
    QListWidget* current_list = current_song_list_widget();
    if (current_list == nullptr || current_list->selectedItems().isEmpty())
    {
        return;
    }

    QList<int> indices_to_remove;
    bool is_playing_item_deleted = false;
    for (QListWidgetItem* item : current_list->selectedItems())
    {
        if (item->data(Qt::UserRole).toString() == current_playing_file_path_)
        {
            is_playing_item_deleted = true;
        }
        indices_to_remove.append(current_list->row(item));
    }

    playlist_manager_->remove_songs_from_playlist(current_playlist_id_, indices_to_remove);

    if (is_playing_item_deleted)
    {
        LOG_INFO("正在播放的项目被删除，停止播放并清理UI");
        controller_->stop();
        clear_playing_indicator();
        setWindowTitle("音乐播放器");
        file_path_label_->clear();
        progress_slider_->setValue(0);
        time_label_->setText("00:00 / 00:00");
    }
}

void mainwindow::on_playlist_context_menu_requested(const QPoint& pos)
{
    auto* current_list = current_song_list_widget();
    if (current_list == nullptr)
    {
        return;
    }

    QMenu context_menu;
    QAction* add_action = context_menu.addAction("添加音乐到当前列表...");
    connect(add_action, &QAction::triggered, this, &mainwindow::on_add_songs_requested);
    context_menu.addSeparator();

    QAction* delete_action = context_menu.addAction("从列表中删除");
    delete_action->setEnabled(!current_list->selectedItems().isEmpty());
    connect(delete_action, &QAction::triggered, this, &mainwindow::on_remove_songs_requested);

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

    QMenu context_menu(this);
    QAction* rename_action = context_menu.addAction("重命名");
    connect(rename_action, &QAction::triggered, this, &mainwindow::on_rename_playlist_requested);

    QAction* delete_action = context_menu.addAction("删除播放列表 \"" + button->text() + "\"");
    connect(delete_action, &QAction::triggered, this, &mainwindow::on_delete_playlist_requested);

    context_menu.exec(button->mapToGlobal(pos));
}

QListWidget* mainwindow::current_song_list_widget() const
{
    if (main_stack_widget_->currentWidget() != player_view_widget_ || current_playlist_id_.isEmpty())
    {
        return nullptr;
    }
    return playlist_widgets_.value(current_playlist_id_, nullptr);
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

    for (const auto& playlist : playlist_manager_->get_all_playlists())
    {
        auto* source_item = new QListWidgetItem(playlist.name);
        source_item->setData(Qt::UserRole, playlist.id);
        mgmt_source_playlists_->addItem(source_item);

        auto* dest_item = new QListWidgetItem(playlist.name);
        dest_item->setData(Qt::UserRole, playlist.id);
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
    Playlist playlist = playlist_manager_->get_playlist_by_id(playlist_id);
    for (const auto& song : playlist.songs)
    {
        auto* new_item = new QListWidgetItem(song.fileName);
        new_item->setData(Qt::UserRole, song.filePath);
        mgmt_source_songs_->addItem(new_item);
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
    Playlist playlist = playlist_manager_->get_playlist_by_id(playlist_id);
    for (const auto& song : playlist.songs)
    {
        auto* new_item = new QListWidgetItem(song.fileName);
        new_item->setData(Qt::UserRole, song.filePath);
        mgmt_dest_songs_->addItem(new_item);
    }
}

void mainwindow::add_selected_songs_to_playlist()
{
    QStringList songs_to_add;
    for (int i = 0; i < mgmt_source_songs_->count(); ++i)
    {
        QListWidgetItem* item = mgmt_source_songs_->item(i);
        if (item != nullptr && item->isSelected())
        {
            songs_to_add.append(item->data(Qt::UserRole).toString());
        }
    }

    QListWidgetItem* dest_playlist_item = mgmt_dest_playlists_->currentItem();

    if (songs_to_add.isEmpty() || dest_playlist_item == nullptr)
    {
        LOG_WARN("添加歌曲失败：未选择歌曲或未选择目标播放列表");
        return;
    }

    QString dest_playlist_id = dest_playlist_item->data(Qt::UserRole).toString();
    playlist_manager_->add_songs_to_playlist(dest_playlist_id, songs_to_add);
    update_management_dest_songs(dest_playlist_item);
}
