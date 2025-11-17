#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QMenu>
#include <QAction>
#include <QRandomGenerator>
#include <QFont>
#include <QCollator>
#include <QPushButton>
#include <QMoveEvent>
#include <QEvent>
#include <QMouseEvent>

#include "tray_icon.h"
#include "quick_editor.h"
#include "playlist_window.h"
#include "playlist_manager.h"
#include "playback_controller.h"
#include "music_management_dialog.h"

static QTreeWidgetItem* find_item_by_id(QTreeWidget* tree, qint64 id)
{
    if (tree == nullptr || id == -1)
    {
        return nullptr;
    }
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* item = tree->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toLongLong() == id)
        {
            return item;
        }
    }
    return nullptr;
}

playlist_window::playlist_window(QWidget* parent) : QMainWindow(parent)
{
    setWindowFlags(Qt::FramelessWindowHint);

    controller_ = new playback_controller(this);
    playlist_manager_ = new playlist_manager(this);
    player_window_ = new player_window(controller_, this);
    player_window_->set_attach(is_player_attached_);

    setup_ui();
    setup_connections();

    playlist_manager_->initialize_and_load();
    populate_playlists_on_startup();
    setWindowTitle("音乐播放器 - 播放列表");
    centralWidget()->installEventFilter(this);
    song_tree_widget_->installEventFilter(this);
    resize(480, 300);
}

playlist_window::~playlist_window()
{
    if (player_window_ != nullptr)
    {
        player_window_->close();
    }
}

void playlist_window::quit_application()
{
    hide();
    if (player_window_ != nullptr)
    {
        player_window_->hide();
    }
    QApplication::quit();
}

void playlist_window::closeEvent(QCloseEvent* event)
{
    if (tray_icon_->isVisible())
    {
        hide();
        if (player_window_ != nullptr)
        {
            player_window_->hide();
        }
        event->ignore();
    }
    else
    {
        event->accept();
    }
}

void playlist_window::moveEvent(QMoveEvent* event)
{
    QMainWindow::moveEvent(event);
    if (is_player_attached_)
    {
        update_player_window_position();
    }
}

bool playlist_window::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == centralWidget() || watched == song_tree_widget_)
    {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (event->type() == QEvent::MouseButtonPress)
        {
            if (mouseEvent->button() == Qt::LeftButton)
            {
                is_being_dragged_by_user_ = true;
                drag_position_ = mouseEvent->globalPosition().toPoint() - frameGeometry().topLeft();
                return true;
            }
        }
        else if (event->type() == QEvent::MouseMove)
        {
            if (is_being_dragged_by_user_)
            {
                move(mouseEvent->globalPosition().toPoint() - drag_position_);
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonRelease)
        {
            if (mouseEvent->button() == Qt::LeftButton)
            {
                is_being_dragged_by_user_ = false;
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void playlist_window::setup_ui()
{
    tray_icon_ = new tray_icon(this);
    tray_icon_->show();

    auto* central_widget = new QWidget(this);
    setCentralWidget(central_widget);
    auto* main_layout = new QVBoxLayout(central_widget);
    main_layout->setContentsMargins(5, 5, 5, 5);

    song_tree_widget_ = new QTreeWidget();
    song_tree_widget_->setObjectName("songTreeWidget");
    song_tree_widget_->setColumnCount(1);
    song_tree_widget_->header()->hide();
    song_tree_widget_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    song_tree_widget_->setIndentation(10);
    song_tree_widget_->setContextMenuPolicy(Qt::CustomContextMenu);
    song_tree_widget_->setSelectionMode(QAbstractItemView::ExtendedSelection);

    auto* manage_button = new QPushButton(QIcon(":/icons/manage.svg"), " 管理音乐");
    manage_button->setIconSize(QSize(18, 18));

    toggle_player_window_button_ = new QPushButton("显示/隐藏播放器");
    toggle_player_window_button_->setToolTip("切换播放器窗口的可见性");
    toggle_player_window_button_->setEnabled(false);

    auto* button_layout = new QHBoxLayout();
    button_layout->addWidget(toggle_player_window_button_);
    button_layout->addStretch();
    button_layout->addWidget(manage_button);

    main_layout->addWidget(song_tree_widget_);
    main_layout->addLayout(button_layout);

    connect(manage_button, &QPushButton::clicked, this, &playlist_window::on_manage_playlists_action);
    connect(tray_icon_,
            &tray_icon::show_hide_triggered,
            this,
            [this]()
            {
                if (isVisible())
                {
                    hide();
                }
                else
                {
                    show();
                }
            });
    connect(tray_icon_, &tray_icon::quit_triggered, this, &playlist_window::quit_application);
}

void playlist_window::setup_connections()
{
    connect(song_tree_widget_, &QTreeWidget::itemDoubleClicked, this, &playlist_window::on_tree_item_double_clicked);
    connect(song_tree_widget_, &QTreeWidget::customContextMenuRequested, this, &playlist_window::on_song_tree_context_menu_requested);
    connect(toggle_player_window_button_, &QPushButton::clicked, this, &playlist_window::on_toggle_player_window_clicked);

    connect(playlist_manager_, &playlist_manager::playlist_added, this, &playlist_window::on_playlist_added);
    connect(playlist_manager_, &playlist_manager::playlist_removed, this, &playlist_window::on_playlist_removed);
    connect(playlist_manager_, &playlist_manager::playlist_renamed, this, &playlist_window::on_playlist_renamed);
    connect(playlist_manager_, &playlist_manager::songs_changed_in_playlist, this, &playlist_window::on_songs_changed);

    connect(controller_, &playback_controller::playback_started, this, &playlist_window::on_playback_started);
    connect(controller_, &playback_controller::playback_finished, this, &playlist_window::handle_playback_finished);

    connect(player_window_, &player_window::next_requested, this, &playlist_window::on_next_requested);
    connect(player_window_, &player_window::previous_requested, this, &playlist_window::on_previous_requested);
    connect(player_window_, &player_window::stop_requested, this, &playlist_window::on_stop_requested);
    connect(player_window_, &player_window::playback_mode_changed, this, &playlist_window::on_playback_mode_changed);

    connect(player_window_, &player_window::request_snap, this, &playlist_window::on_player_request_snap);
    connect(player_window_, &player_window::request_detach, this, &playlist_window::on_player_request_detach);
    connect(player_window_, &player_window::request_resnap, this, &playlist_window::on_player_request_resnap);
}

void playlist_window::on_player_request_resnap()
{
    if (!is_player_attached_)
    {
        is_player_attached_ = true;
        player_window_->set_attach(true);
    }
    update_player_window_position();
}

void playlist_window::on_player_request_detach()
{
    if (!is_player_attached_)
    {
        return;
    }

    is_player_attached_ = false;
    player_window_->set_attach(false);
}

void playlist_window::on_player_request_snap(snap_side side)
{
    if (is_player_attached_ && current_snap_side_ == side)
    {
        return;
    }

    is_player_attached_ = true;
    current_snap_side_ = side;
    player_window_->set_attach(true);

    update_player_window_position();
}

void playlist_window::on_playback_mode_changed(playback_mode new_mode)
{
    current_mode_ = new_mode;
    if (current_mode_ == playback_mode::Shuffle && currently_playing_item_ != nullptr)
    {
        QTreeWidgetItem* playlist_item = currently_playing_item_->parent();
        int current_song_index = playlist_item->indexOfChild(currently_playing_item_);
        generate_shuffled_list(playlist_item, current_song_index);
        current_shuffle_index_ = 0;
    }
    else if (current_mode_ != playback_mode::Shuffle)
    {
        shuffled_indices_.clear();
        current_shuffle_index_ = -1;
    }
}

void playlist_window::on_stop_requested()
{
    controller_->stop();
    clear_playing_indicator();
    current_playing_file_path_.clear();
    shuffled_indices_.clear();
    current_shuffle_index_ = -1;
    player_window_->on_playback_stopped();
    toggle_player_window_button_->setEnabled(false);
}

void playlist_window::on_playback_started(const QString& file_path, const QString& file_name)
{
    (void)file_path;
    (void)file_name;
    clear_playing_indicator();

    if (clicked_song_item_ != nullptr && clicked_song_item_->data(0, Qt::UserRole).toString() == current_playing_file_path_)
    {
        currently_playing_item_ = clicked_song_item_;
        QFont font = currently_playing_item_->font(0);
        font.setBold(true);
        currently_playing_item_->setFont(0, font);
        currently_playing_item_->setForeground(0, QBrush(QColor("#3498DB")));
        song_tree_widget_->scrollToItem(currently_playing_item_, QAbstractItemView::PositionAtCenter);
    }
    if (!player_window_->isVisible())
    {
        is_player_attached_ = true;
        player_window_->set_attach(true);
        current_snap_side_ = snap_side::right;
        update_player_window_position();
        player_window_->show();
    }
    toggle_player_window_button_->setEnabled(true);
}

void playlist_window::generate_shuffled_list(QTreeWidgetItem* playlist_item, int start_song_index)
{
    if (playlist_item == nullptr)
    {
        return;
    }
    shuffled_indices_.clear();
    const int song_count = playlist_item->childCount();
    if (song_count == 0)
    {
        return;
    }

    for (int i = 0; i < song_count; ++i)
    {
        shuffled_indices_.append(i);
    }

    std::shuffle(shuffled_indices_.begin(), shuffled_indices_.end(), *QRandomGenerator::global());

    if (start_song_index != -1)
    {
        const int current_pos = static_cast<int>(shuffled_indices_.indexOf(start_song_index));
        if (current_pos != -1)
        {
            shuffled_indices_.swapItemsAt(0, current_pos);
        }
    }
}

void playlist_window::on_manage_playlists_action()
{
    auto* dialog = new music_management_dialog(playlist_manager_, this);
    dialog->exec();
}

void playlist_window::on_toggle_player_window_clicked()
{
    if (player_window_ == nullptr)
    {
        return;
    }
    if (player_window_->isVisible())
    {
        last_player_pos_ = player_window_->pos();
        player_window_->hide();
    }
    else
    {
        if (!last_player_pos_.isNull())
        {
            player_window_->move(last_player_pos_);
        }
        else
        {
            is_player_attached_ = true;
            player_window_->set_attach(true);
            current_snap_side_ = snap_side::right;
            update_player_window_position();
        }
        player_window_->show();
    }
}

void playlist_window::update_player_window_position()
{
    if (player_window_ == nullptr || !is_player_attached_)
    {
        return;
    }

    QRect main_rect = this->frameGeometry();
    QRect player_rect = player_window_->frameGeometry();
    QPoint target_pos;

    switch (current_snap_side_)
    {
        case snap_side::right:
            target_pos.setX(main_rect.right());
            target_pos.setY(main_rect.top());
            break;
        case snap_side::left:
            target_pos.setX(main_rect.left() - player_rect.width());
            target_pos.setY(main_rect.top());
            break;
        case snap_side::top:
            target_pos.setX(main_rect.left());
            target_pos.setY(main_rect.top() - player_rect.height());
            break;
        case snap_side::bottom:
            target_pos.setX(main_rect.left());
            target_pos.setY(main_rect.bottom());
            break;
        case snap_side::none:
            return;
    }

    player_window_->move(target_pos);
}

void playlist_window::populate_playlists_on_startup()
{
    song_tree_widget_->blockSignals(true);
    song_tree_widget_->clear();
    QList<Playlist> playlists = playlist_manager_->get_all_playlists();
    for (const auto& playlist : playlists)
    {
        Playlist full_playlist = playlist_manager_->get_playlist_by_id(playlist.id);
        auto* playlist_item = new QTreeWidgetItem(song_tree_widget_);
        playlist_item->setText(0, QString("%1 [%2]").arg(playlist.name).arg(full_playlist.songs.count()));
        playlist_item->setData(0, Qt::UserRole, playlist.id);
        playlist_item->setIcon(0, QIcon(":/icons/playlist.svg"));
        playlist_item->setExpanded(false);

        for (const auto& song : full_playlist.songs)
        {
            auto* song_item = new QTreeWidgetItem(playlist_item);
            song_item->setIcon(0, QIcon(":/icons/song.svg"));
            song_item->setText(0, song.file_name);
            song_item->setData(0, Qt::UserRole, song.file_path);
        }
    }
    song_tree_widget_->blockSignals(false);
}

void playlist_window::on_playlist_added(const Playlist& new_playlist)
{
    song_tree_widget_->blockSignals(true);
    auto* playlist_item = new QTreeWidgetItem(song_tree_widget_);
    playlist_item->setText(0, QString("%1 [0]").arg(new_playlist.name));
    playlist_item->setData(0, Qt::UserRole, new_playlist.id);
    playlist_item->setIcon(0, QIcon(":/icons/playlist.svg"));
    playlist_item->setExpanded(false);
    song_tree_widget_->blockSignals(false);
}

void playlist_window::on_playlist_removed(qint64 playlist_id)
{
    QTreeWidgetItem* item = find_item_by_id(song_tree_widget_, playlist_id);
    delete item;
}

void playlist_window::on_playlist_renamed(qint64 playlist_id) { on_songs_changed(playlist_id); }

void playlist_window::on_songs_changed(qint64 playlist_id)
{
    QTreeWidgetItem* item = find_item_by_id(song_tree_widget_, playlist_id);
    if (item != nullptr)
    {
        const Playlist playlist = playlist_manager_->get_playlist_by_id(playlist_id);
        item->takeChildren();
        for (const auto& song : playlist.songs)
        {
            auto* song_item = new QTreeWidgetItem(item);
            song_item->setIcon(0, QIcon(":/icons/song.svg"));
            song_item->setText(0, song.file_name);
            song_item->setData(0, Qt::UserRole, song.file_path);
        }
        item->setText(0, QString("%1 [%2]").arg(playlist.name).arg(playlist.songs.count()));
    }
}

void playlist_window::on_song_tree_context_menu_requested(const QPoint& pos)
{
    context_menu_item_ = song_tree_widget_->itemAt(pos);

    QMenu context_menu(this);

    if (context_menu_item_ == nullptr)
    {
        auto* new_playlist_action = context_menu.addAction("新建播放列表");
        connect(new_playlist_action, &QAction::triggered, this, &playlist_window::on_create_playlist_action);
    }
    else if (context_menu_item_->parent() == nullptr)
    {
        auto* add_songs_action = context_menu.addAction("添加歌曲");
        auto* sort_playlist_action = context_menu.addAction("排序列表");
        context_menu.addSeparator();
        auto* rename_action = context_menu.addAction("重命名");
        auto* delete_action = context_menu.addAction("删除播放列表");
        context_menu.addSeparator();
        auto* new_playlist_action = context_menu.addAction("新建播放列表");

        connect(add_songs_action, &QAction::triggered, this, &playlist_window::on_add_songs_action);
        connect(sort_playlist_action, &QAction::triggered, this, &playlist_window::on_sort_playlist_action);
        connect(rename_action, &QAction::triggered, this, &playlist_window::on_rename_playlist_action);
        connect(delete_action, &QAction::triggered, this, &playlist_window::on_delete_playlist_action);
        connect(new_playlist_action, &QAction::triggered, this, &playlist_window::on_create_playlist_action);
    }
    else
    {
        auto* remove_songs_action = context_menu.addAction("从播放列表移除");
        connect(remove_songs_action, &QAction::triggered, this, &playlist_window::on_remove_songs_action);
    }

    context_menu.exec(song_tree_widget_->viewport()->mapToGlobal(pos));
}

void playlist_window::on_create_playlist_action()
{
    is_creating_playlist_ = true;

    auto* editor = new quick_editor("新建播放列表", this);
    connect(editor, &quick_editor::editing_finished, this, &playlist_window::on_editing_finished);

    const QPoint pos = song_tree_widget_->viewport()->mapToGlobal(song_tree_widget_->rect().center());
    editor->move(pos.x() - (editor->width() / 2), pos.y() - (editor->height() / 2));
    editor->show();
}

void playlist_window::on_rename_playlist_action()
{
    if (context_menu_item_ == nullptr)
    {
        return;
    }
    is_creating_playlist_ = false;

    const qint64 playlist_id = context_menu_item_->data(0, Qt::UserRole).toLongLong();
    const Playlist playlist = playlist_manager_->get_playlist_by_id(playlist_id);

    auto* editor = new quick_editor(playlist.name, this);
    connect(editor, &quick_editor::editing_finished, this, &playlist_window::on_editing_finished);

    const QRect item_rect = song_tree_widget_->visualItemRect(context_menu_item_);
    const QPoint pos = song_tree_widget_->viewport()->mapToGlobal(item_rect.topLeft());
    editor->move(pos);
    editor->show();
}

void playlist_window::on_editing_finished(bool accepted, const QString& text)
{
    if (!accepted)
    {
        return;
    }

    const QString new_name = text.trimmed();
    if (new_name.isEmpty())
    {
        QMessageBox::warning(this, "无效名称", "播放列表名称不能为空");
        return;
    }

    if (is_creating_playlist_)
    {
        playlist_manager_->create_new_playlist(new_name);
    }
    else
    {
        if (context_menu_item_ != nullptr)
        {
            const qint64 playlist_id = context_menu_item_->data(0, Qt::UserRole).toLongLong();
            playlist_manager_->rename_playlist(playlist_id, new_name);
        }
    }
}

void playlist_window::on_delete_playlist_action()
{
    if (context_menu_item_ == nullptr)
    {
        return;
    }
    const qint64 playlist_id = context_menu_item_->data(0, Qt::UserRole).toLongLong();
    const QString playlist_name = playlist_manager_->get_playlist_by_id(playlist_id).name;

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(
        this, "确认删除", QString("您确定要删除播放列表 '%1' 吗？此操作无法撤销。").arg(playlist_name), QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes)
    {
        playlist_manager_->delete_playlist(playlist_id);
    }
}

void playlist_window::on_add_songs_action()
{
    if (context_menu_item_ == nullptr || context_menu_item_->parent() != nullptr)
    {
        return;
    }
    const qint64 playlist_id = context_menu_item_->data(0, Qt::UserRole).toLongLong();

    const QStringList files =
        QFileDialog::getOpenFileNames(this, "选择要添加的音乐文件", "", "音频文件 (*.mp3 *.flac *.wav *.m4a *.ogg *.mp4 *.webm)");

    if (!files.isEmpty())
    {
        playlist_manager_->add_songs_to_playlist(playlist_id, files);
    }
}

void playlist_window::on_remove_songs_action()
{
    const QList<QTreeWidgetItem*> selected_items = song_tree_widget_->selectedItems();
    if (selected_items.isEmpty())
    {
        return;
    }

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
        const qint64 playlist_id = parent_playlist_item->data(0, Qt::UserRole).toLongLong();
        playlist_manager_->remove_songs_from_playlist(playlist_id, indices_to_remove);
    }
}

void playlist_window::on_sort_playlist_action()
{
    if (context_menu_item_ == nullptr || context_menu_item_->parent() != nullptr)
    {
        return;
    }
    const qint64 playlist_id = context_menu_item_->data(0, Qt::UserRole).toLongLong();
    playlist_manager_->sort_playlist(playlist_id);
}

void playlist_window::on_tree_item_double_clicked(QTreeWidgetItem* item, int column)
{
    if (item == nullptr || item->parent() == nullptr || column != 0)
    {
        return;
    }
    current_playing_file_path_ = item->data(0, Qt::UserRole).toString();
    clicked_song_item_ = item;

    playlist_manager_->increment_play_count(current_playing_file_path_);

    if (current_mode_ == playback_mode::Shuffle)
    {
        QTreeWidgetItem* playlist_item = item->parent();
        const int clicked_song_index = playlist_item->indexOfChild(item);
        generate_shuffled_list(playlist_item, clicked_song_index);
        current_shuffle_index_ = 0;
    }
    controller_->play_file(current_playing_file_path_);
}

void playlist_window::on_next_requested()
{
    if (currently_playing_item_ == nullptr)
    {
        play_first_song_in_list();
        return;
    }

    if (current_mode_ == playback_mode::Shuffle)
    {
        QTreeWidgetItem* playlist_item = currently_playing_item_->parent();
        if (playlist_item == nullptr || shuffled_indices_.isEmpty())
        {
            return;
        }

        current_shuffle_index_++;
        if (current_shuffle_index_ >= shuffled_indices_.size())
        {
            generate_shuffled_list(playlist_item);
            current_shuffle_index_ = 0;
        }
        if (shuffled_indices_.isEmpty())
        {
            return;
        }
        const int next_song_index = shuffled_indices_.at(current_shuffle_index_);
        QTreeWidgetItem* next_item = playlist_item->child(next_song_index);
        on_tree_item_double_clicked(next_item, 0);
        return;
    }

    QTreeWidgetItem* next_item = song_tree_widget_->itemBelow(currently_playing_item_);

    if (next_item == nullptr || next_item->parent() != currently_playing_item_->parent())
    {
        if (current_mode_ == playback_mode::ListLoop)
        {
            next_item = currently_playing_item_->parent()->child(0);
        }
        else
        {
            return;
        }
    }

    if (next_item != nullptr && next_item->parent() != nullptr)
    {
        song_tree_widget_->setCurrentItem(next_item);
        on_tree_item_double_clicked(next_item, 0);
    }
}

void playlist_window::on_previous_requested()
{
    if (currently_playing_item_ == nullptr)
    {
        play_first_song_in_list();
        return;
    }

    if (current_mode_ == playback_mode::Shuffle)
    {
        QTreeWidgetItem* playlist_item = currently_playing_item_->parent();
        if (playlist_item == nullptr || shuffled_indices_.isEmpty())
        {
            return;
        }
        current_shuffle_index_--;
        if (current_shuffle_index_ < 0)
        {
            current_shuffle_index_ = static_cast<int>(shuffled_indices_.size()) - 1;
        }
        if (shuffled_indices_.isEmpty())
        {
            return;
        }
        const int prev_song_index = shuffled_indices_.at(current_shuffle_index_);
        QTreeWidgetItem* prev_item = playlist_item->child(prev_song_index);
        on_tree_item_double_clicked(prev_item, 0);
        return;
    }

    QTreeWidgetItem* prev_item = song_tree_widget_->itemAbove(currently_playing_item_);

    if (prev_item == nullptr || prev_item->parent() != currently_playing_item_->parent())
    {
        if (current_mode_ == playback_mode::ListLoop)
        {
            auto* parent_playlist = currently_playing_item_->parent();
            prev_item = parent_playlist->child(parent_playlist->childCount() - 1);
        }
        else
        {
            return;
        }
    }

    if (prev_item != nullptr && prev_item->parent() != nullptr)
    {
        song_tree_widget_->setCurrentItem(prev_item);
        on_tree_item_double_clicked(prev_item, 0);
    }
}

void playlist_window::handle_playback_finished()
{
    switch (current_mode_)
    {
        case playback_mode::ListLoop:
        case playback_mode::Shuffle:
            on_next_requested();
            break;

        case playback_mode::SingleLoop:
            if (currently_playing_item_ != nullptr)
            {
                const QString file_path = currently_playing_item_->data(0, Qt::UserRole).toString();
                controller_->play_file(file_path);
            }
            break;

        case playback_mode::Sequential:
            if (currently_playing_item_ == nullptr)
            {
                on_stop_requested();
                return;
            }

            QTreeWidgetItem* playlist_item = currently_playing_item_->parent();
            if (playlist_item != nullptr)
            {
                int current_index = playlist_item->indexOfChild(currently_playing_item_);
                int song_count = playlist_item->childCount();
                if (current_index == song_count - 1)
                {
                    on_stop_requested();
                }
                else
                {
                    on_next_requested();
                }
            }
            else
            {
                on_stop_requested();
            }
            break;
    }
}

void playlist_window::clear_playing_indicator()
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

void playlist_window::play_first_song_in_list()
{
    if (song_tree_widget_->topLevelItemCount() > 0)
    {
        auto* first_playlist = song_tree_widget_->topLevelItem(0);
        if (first_playlist->childCount() > 0)
        {
            on_tree_item_double_clicked(first_playlist->child(0), 0);
        }
    }
}
