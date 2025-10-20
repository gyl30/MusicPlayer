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
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QStyle>
#include <QTime>
#include <QMenu>
#include <QAction>
#include <QFont>

#include "log.h"
#include "mainwindow.h"
#include "volumemeter.h"
#include "spectrum_widget.h"
#include "playlist_manager.h"
#include "playback_controller.h"
#include "quick_editor.h"

static QTreeWidgetItem* find_item_by_id(QTreeWidget* tree, const QString& id)
{
    if (tree == nullptr || id.isEmpty())
    {
        return nullptr;
    }
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* item = tree->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == id)
        {
            return item;
        }
    }
    return nullptr;
}

mainwindow::mainwindow(QWidget* parent) : QMainWindow(parent)
{
    playlist_manager_ = new playlist_manager(this);
    controller_ = new playback_controller(this);

    setup_ui();
    setup_connections();

    controller_->set_spectrum_widget(spectrum_widget_);

    playlist_manager_->load_playlists();
    populate_playlists_on_startup();
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
    main_layout->setContentsMargins(10, 10, 10, 10);
    main_layout->setSpacing(0);

    song_tree_widget_ = new QTreeWidget();
    song_tree_widget_->setObjectName("songTreeWidget");
    song_tree_widget_->setColumnCount(1);
    song_tree_widget_->header()->hide();
    song_tree_widget_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    song_tree_widget_->setIndentation(10);
    song_tree_widget_->setContextMenuPolicy(Qt::CustomContextMenu);
    song_tree_widget_->setSelectionMode(QAbstractItemView::ExtendedSelection);

    auto* bottom_container = new QWidget();
    bottom_container->setObjectName("bottomContainer");
    bottom_container->setFixedHeight(150);

    auto* bottom_h_layout = new QHBoxLayout(bottom_container);
    bottom_h_layout->setContentsMargins(0, 0, 0, 0);
    bottom_h_layout->setSpacing(0);

    auto* left_panel = new QWidget();
    auto* main_grid_layout = new QGridLayout(left_panel);
    main_grid_layout->setContentsMargins(0, 10, 10, 5);
    main_grid_layout->setSpacing(5);
    main_grid_layout->setVerticalSpacing(3);

    spectrum_widget_ = new spectrum_widget(this);
    progress_slider_ = new QSlider(Qt::Horizontal);

    stop_button_ = new QPushButton(QIcon(":/icons/stop.svg"), "");
    prev_button_ = new QPushButton(QIcon(":/icons/previous.svg"), "");
    play_pause_button_ = new QPushButton(QIcon(":/icons/play.svg"), "");
    next_button_ = new QPushButton(QIcon(":/icons/next.svg"), "");
    shuffle_button_ = new QPushButton(QIcon(":/icons/shuffle.svg"), "");

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
    shuffle_button_->setToolTip("随机播放");

    song_title_label_ = new QLabel("Music Player", this);
    song_title_label_->setObjectName("songTitleLabel");

    time_label_ = new QLabel("00:00 / 00:00", this);
    time_label_->setFixedWidth(120);
    time_label_->setAlignment(Qt::AlignCenter);

    auto* all_buttons_container = new QWidget();
    auto* button_grid_layout = new QGridLayout(all_buttons_container);
    button_grid_layout->setContentsMargins(0, 0, 0, 0);
    button_grid_layout->setSpacing(10);

    auto* left_buttons_group = new QWidget();
    auto* left_layout = new QHBoxLayout(left_buttons_group);
    left_layout->setContentsMargins(0, 0, 0, 0);
    left_layout->setSpacing(10);
    left_layout->addWidget(shuffle_button_);
    left_layout->addWidget(prev_button_);

    auto* right_buttons_group = new QWidget();
    auto* right_layout = new QHBoxLayout(right_buttons_group);
    right_layout->setContentsMargins(0, 0, 0, 0);
    right_layout->setSpacing(10);
    right_layout->addWidget(next_button_);
    right_layout->addWidget(stop_button_);

    button_grid_layout->addWidget(left_buttons_group, 0, 0, Qt::AlignRight);
    button_grid_layout->addWidget(play_pause_button_, 0, 1, Qt::AlignCenter);
    button_grid_layout->addWidget(right_buttons_group, 0, 2, Qt::AlignLeft);

    button_grid_layout->setColumnStretch(0, 1);
    button_grid_layout->setColumnStretch(2, 1);

    main_grid_layout->addWidget(spectrum_widget_, 0, 0, 1, 3);
    main_grid_layout->addWidget(progress_slider_, 1, 0, 1, 3);
    main_grid_layout->addWidget(song_title_label_, 2, 0, Qt::AlignLeft | Qt::AlignVCenter);
    main_grid_layout->addWidget(all_buttons_container, 2, 1, Qt::AlignCenter);
    main_grid_layout->addWidget(time_label_, 2, 2, Qt::AlignRight | Qt::AlignVCenter);

    main_grid_layout->setColumnStretch(0, 1);
    main_grid_layout->setColumnStretch(1, 0);
    main_grid_layout->setColumnStretch(2, 1);
    main_grid_layout->setRowStretch(0, 1);

    volume_meter_ = new volume_meter();
    volume_meter_->setFixedWidth(8);
    volume_meter_->setRange(0, 100);
    volume_meter_->setValue(80);
    volume_meter_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    volume_meter_->setOrientation(Qt::Vertical);
    volume_meter_->setToolTip("音量");

    bottom_h_layout->addWidget(left_panel, 1);
    bottom_h_layout->addWidget(volume_meter_);

    main_layout->addWidget(bottom_container); // top
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
    connect(song_tree_widget_, &QTreeWidget::customContextMenuRequested, this, &mainwindow::on_song_tree_context_menu_requested);

    connect(controller_, &playback_controller::track_info_ready, this, &mainwindow::update_track_info);
    connect(controller_, &playback_controller::playback_started, this, &mainwindow::on_playback_started);
    connect(controller_, &playback_controller::progress_updated, this, &mainwindow::update_progress);
    connect(controller_, &playback_controller::playback_finished, this, &mainwindow::handle_playback_finished);
    connect(controller_, &playback_controller::playback_error, this, &mainwindow::handle_playback_error);

    connect(playlist_manager_, &playlist_manager::playlist_added, this, &mainwindow::on_playlist_added);
    connect(playlist_manager_, &playlist_manager::playlist_removed, this, &mainwindow::on_playlist_removed);
    connect(playlist_manager_, &playlist_manager::playlist_renamed, this, &mainwindow::on_playlist_renamed);
    connect(playlist_manager_, &playlist_manager::songs_changed_in_playlist, this, &mainwindow::on_songs_changed);
}

void mainwindow::populate_playlists_on_startup()
{
    LOG_DEBUG("首次从数据填充播放列表ui");
    song_tree_widget_->blockSignals(true);
    song_tree_widget_->clear();
    for (const auto& playlist : playlist_manager_->get_all_playlists())
    {
        auto* playlist_item = new QTreeWidgetItem(song_tree_widget_);
        playlist_item->setText(0, QString("%1 [%2]").arg(playlist.name).arg(playlist.songs.count()));
        playlist_item->setData(0, Qt::UserRole, playlist.id);
        playlist_item->setIcon(0, QIcon(":/icons/playlist.svg"));
        playlist_item->setExpanded(true);

        for (const auto& song : playlist.songs)
        {
            auto* song_item = new QTreeWidgetItem(playlist_item);
            song_item->setIcon(0, QIcon(":/icons/song.svg"));
            song_item->setText(0, song.fileName);
            song_item->setData(0, Qt::UserRole, song.filePath);
        }
    }
    song_tree_widget_->blockSignals(false);
}

void mainwindow::on_playlist_added(const Playlist& new_playlist)
{
    LOG_DEBUG("ui添加新播放列表 id {}", new_playlist.id.toStdString());
    song_tree_widget_->blockSignals(true);
    auto* playlist_item = new QTreeWidgetItem(song_tree_widget_);
    playlist_item->setText(0, QString("%1 [0]").arg(new_playlist.name));
    playlist_item->setData(0, Qt::UserRole, new_playlist.id);
    playlist_item->setIcon(0, QIcon(":/icons/playlist.svg"));
    playlist_item->setExpanded(true);
    song_tree_widget_->blockSignals(false);
}

void mainwindow::on_playlist_removed(const QString& playlist_id)
{
    LOG_DEBUG("ui移除播放列表 id {}", playlist_id.toStdString());
    QTreeWidgetItem* item = find_item_by_id(song_tree_widget_, playlist_id);
    delete item;
}

void mainwindow::on_playlist_renamed(const QString& playlist_id)
{
    LOG_DEBUG("ui重命名播放列表 id {}", playlist_id.toStdString());
    on_songs_changed(playlist_id);
}

void mainwindow::on_songs_changed(const QString& playlist_id)
{
    LOG_DEBUG("ui更新播放列表歌曲 id {}", playlist_id.toStdString());
    QTreeWidgetItem* item = find_item_by_id(song_tree_widget_, playlist_id);
    if (item != nullptr)
    {
        const Playlist playlist = playlist_manager_->get_playlist_by_id(playlist_id);
        item->takeChildren();
        for (const auto& song : playlist.songs)
        {
            auto* song_item = new QTreeWidgetItem(item);
            song_item->setIcon(0, QIcon(":/icons/song.svg"));
            song_item->setText(0, song.fileName);
            song_item->setData(0, Qt::UserRole, song.filePath);
        }
        item->setText(0, QString("%1 [%2]").arg(playlist.name).arg(playlist.songs.count()));
    }
}

void mainwindow::on_song_tree_context_menu_requested(const QPoint& pos)
{
    context_menu_item_ = song_tree_widget_->itemAt(pos);
    LOG_DEBUG("右键菜单请求 位置 ({}, {})", pos.x(), pos.y());

    QMenu context_menu(this);

    if (context_menu_item_ == nullptr)
    {
        LOG_DEBUG("在空白区域创建菜单");
        auto* new_playlist_action = context_menu.addAction("新建播放列表");
        connect(new_playlist_action, &QAction::triggered, this, &mainwindow::on_create_playlist_action);
    }
    else if (context_menu_item_->parent() == nullptr)
    {
        LOG_DEBUG("在播放列表项上创建菜单");
        auto* add_songs_action = context_menu.addAction("添加歌曲");
        context_menu.addSeparator();
        auto* rename_action = context_menu.addAction("重命名");
        auto* delete_action = context_menu.addAction("删除播放列表");
        context_menu.addSeparator();
        auto* new_playlist_action = context_menu.addAction("新建播放列表");

        connect(add_songs_action, &QAction::triggered, this, &mainwindow::on_add_songs_action);
        connect(rename_action, &QAction::triggered, this, &mainwindow::on_rename_playlist_action);
        connect(delete_action, &QAction::triggered, this, &mainwindow::on_delete_playlist_action);
        connect(new_playlist_action, &QAction::triggered, this, &mainwindow::on_create_playlist_action);
    }
    else
    {
        LOG_DEBUG("在歌曲项上创建菜单");
        auto* remove_songs_action = context_menu.addAction("从播放列表移除");
        connect(remove_songs_action, &QAction::triggered, this, &mainwindow::on_remove_songs_action);
    }

    context_menu.exec(song_tree_widget_->viewport()->mapToGlobal(pos));
}

void mainwindow::on_create_playlist_action()
{
    LOG_INFO("动作触发 创建新播放列表");
    is_creating_playlist_ = true;

    auto* editor = new quick_editor("新建播放列表", this);
    connect(editor, &quick_editor::editing_finished, this, &mainwindow::on_editing_finished);

    const QPoint pos = song_tree_widget_->viewport()->mapToGlobal(song_tree_widget_->rect().center());
    editor->move(pos.x() - (editor->width() / 2), pos.y() - (editor->height() / 2));
    editor->show();
}

void mainwindow::on_rename_playlist_action()
{
    if (context_menu_item_ == nullptr)
    {
        return;
    }
    is_creating_playlist_ = false;

    const QString playlist_id = context_menu_item_->data(0, Qt::UserRole).toString();
    const Playlist playlist = playlist_manager_->get_playlist_by_id(playlist_id);
    LOG_INFO("动作触发 重命名播放列表 id {}", playlist_id.toStdString());

    auto* editor = new quick_editor(playlist.name, this);
    connect(editor, &quick_editor::editing_finished, this, &mainwindow::on_editing_finished);

    const QRect item_rect = song_tree_widget_->visualItemRect(context_menu_item_);
    const QPoint pos = song_tree_widget_->viewport()->mapToGlobal(item_rect.topLeft());
    editor->move(pos);
    editor->show();
}

void mainwindow::on_editing_finished(bool accepted, const QString& text)
{
    if (!accepted)
    {
        LOG_INFO("用户取消编辑");
        return;
    }

    const QString new_name = text.trimmed();
    if (new_name.isEmpty())
    {
        LOG_WARN("用户输入为空名称");
        QMessageBox::warning(this, "无效名称", "播放列表名称不能为空");
        return;
    }

    if (is_creating_playlist_)
    {
        LOG_INFO("用户确认创建新播放列表 名称 {}", new_name.toStdString());
        playlist_manager_->create_new_playlist(new_name);
    }
    else
    {
        if (context_menu_item_ != nullptr)
        {
            const QString playlist_id = context_menu_item_->data(0, Qt::UserRole).toString();
            LOG_INFO("用户确认重命名播放列表 id {} 为 {}", playlist_id.toStdString(), new_name.toStdString());
            playlist_manager_->rename_playlist(playlist_id, new_name);
        }
    }
}

void mainwindow::on_delete_playlist_action()
{
    if (context_menu_item_ == nullptr)
    {
        return;
    }
    const QString playlist_id = context_menu_item_->data(0, Qt::UserRole).toString();
    const QString playlist_name = playlist_manager_->get_playlist_by_id(playlist_id).name;
    LOG_INFO("动作触发 删除播放列表 id {}", playlist_id.toStdString());

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(
        this, "确认删除", QString("您确定要删除播放列表 '%1' 吗？此操作无法撤销。").arg(playlist_name), QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes)
    {
        LOG_INFO("用户确认删除播放列表 id {}", playlist_id.toStdString());
        playlist_manager_->delete_playlist(playlist_id);
    }
    else
    {
        LOG_INFO("用户取消删除播放列表 id {}", playlist_id.toStdString());
    }
}

void mainwindow::on_add_songs_action()
{
    if (context_menu_item_ == nullptr || context_menu_item_->parent() != nullptr)
    {
        return;
    }
    const QString playlist_id = context_menu_item_->data(0, Qt::UserRole).toString();
    LOG_INFO("动作触发 添加歌曲到播放列表 id {}", playlist_id.toStdString());

    const QStringList files = QFileDialog::getOpenFileNames(this, "选择要添加的音乐文件", "", "音频文件 (*.mp3 *.flac *.wav *.m4a *.ogg)");

    if (!files.isEmpty())
    {
        LOG_INFO("用户选择了 {} 个文件进行添加", files.count());
        playlist_manager_->add_songs_to_playlist(playlist_id, files);
    }
    else
    {
        LOG_INFO("用户取消了文件选择");
    }
}

void mainwindow::on_remove_songs_action()
{
    const QList<QTreeWidgetItem*> selected_items = song_tree_widget_->selectedItems();
    if (selected_items.isEmpty())
    {
        return;
    }
    LOG_INFO("动作触发 移除歌曲");

    QTreeWidgetItem* parent_playlist_item = nullptr;
    QList<int> indices_to_remove;

    for (QTreeWidgetItem* item : selected_items)
    {
        if (item != nullptr && item->parent() != nullptr)
        {
            if (parent_playlist_item == nullptr)
            {
                parent_playlist_item = item->parent();
            }

            if (item->parent() == parent_playlist_item)
            {
                indices_to_remove.append(parent_playlist_item->indexOfChild(item));
            }
        }
    }

    if (parent_playlist_item != nullptr && !indices_to_remove.isEmpty())
    {
        const QString playlist_id = parent_playlist_item->data(0, Qt::UserRole).toString();
        LOG_INFO("准备从播放列表 id {} 中移除 {} 首歌曲", playlist_id.toStdString(), indices_to_remove.count());
        playlist_manager_->remove_songs_from_playlist(playlist_id, indices_to_remove);
    }
    else
    {
        LOG_WARN("没有有效的歌曲项被选中用于移除");
    }
}

void mainwindow::on_tree_item_double_clicked(QTreeWidgetItem* item, int column)
{
    if (item == nullptr || item->parent() == nullptr || column != 0)
    {
        return;
    }
    current_playing_file_path_ = item->data(0, Qt::UserRole).toString();
    clicked_song_item_ = item;
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
        play_pause_button_->setIcon(is_paused_ ? QIcon(":/icons/play.svg") : QIcon(":/icons/pause.svg"));
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
    if (prev_item == nullptr || prev_item->parent() == nullptr)
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
    play_pause_button_->setIcon(QIcon(":/icons/play.svg"));
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
    QString current_time_str = QTime(0, 0).toString(format);
    time_label_->setText(QString("%1 / %2").arg(current_time_str).arg(total_time.toString(format)));
}

void mainwindow::on_playback_started(const QString& file_path, const QString& file_name)
{
    is_playing_ = true;
    is_paused_ = false;
    play_pause_button_->setIcon(QIcon(":/icons/pause.svg"));
    song_title_label_->setText(file_name);
    clear_playing_indicator();

    if (clicked_song_item_ != nullptr && clicked_song_item_->data(0, Qt::UserRole).toString() == file_path)
    {
        currently_playing_item_ = clicked_song_item_;
        QFont font = currently_playing_item_->font(0);
        font.setBold(true);
        currently_playing_item_->setFont(0, font);
        currently_playing_item_->setForeground(0, QBrush(QColor("#3498DB")));
        song_tree_widget_->scrollToItem(currently_playing_item_, QAbstractItemView::PositionAtCenter);
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
    play_pause_button_->setIcon(QIcon(":/icons/play.svg"));
    on_next_clicked();
}

void mainwindow::clear_playing_indicator()
{
    if (currently_playing_item_ != nullptr)
    {
        QFont font = currently_playing_item_->font(0);
        font.setBold(false);
        currently_playing_item_->setFont(0, font);
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
