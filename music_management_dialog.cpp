#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QLabel>
#include <QSet>
#include <QSignalBlocker>
#include <algorithm>
#include "music_management_dialog.h"
#include "playlist_manager.h"

static QListWidgetItem* create_elided_item(const QString& text)
{
    auto* item = new QListWidgetItem(text);
    item->setToolTip(text);
    return item;
}

music_management_dialog::music_management_dialog(playlist_manager* manager, QWidget* parent) : QWidget(parent), playlist_manager_(manager)
{
    setup_ui();
    setup_connections();
}

void music_management_dialog::reload()
{
    load_initial_data();
    populate_playlist_widgets();
}

void music_management_dialog::load_initial_data()
{
    temp_playlists_.clear();
    has_pending_changes_ = false;
    const auto all_playlists = playlist_manager_->get_all_playlists();
    for (const auto& p : all_playlists)
    {
        temp_playlists_.insert(p.id, playlist_manager_->get_playlist_by_id(p.id));
    }
}

void music_management_dialog::setup_ui()
{
    auto* main_layout = new QHBoxLayout(this);
    main_layout->setContentsMargins(16, 16, 16, 16);
    main_layout->setSpacing(10);

    auto* source_panel = new QWidget();
    source_panel->setObjectName("managementPanel");
    auto* source_layout = new QVBoxLayout(source_panel);
    source_layout->addWidget(new QLabel("源歌单"));
    source_playlists_list_ = new QListWidget();
    source_playlists_list_->setTextElideMode(Qt::ElideRight);
    source_playlists_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    source_playlists_list_->setAlternatingRowColors(true);
    source_layout->addWidget(source_playlists_list_, 1);
    auto* source_songs_header_layout = new QHBoxLayout();
    source_songs_header_layout->addWidget(new QLabel("歌曲"));
    source_songs_header_layout->addStretch();
    source_select_all_button_ = new QPushButton("全选");
    source_select_all_button_->setToolTip("勾选当前源歌单中的所有歌曲");
    source_select_all_button_->setEnabled(false);
    source_songs_header_layout->addWidget(source_select_all_button_);
    source_layout->addLayout(source_songs_header_layout);
    source_songs_list_ = new QListWidget();
    source_songs_list_->setTextElideMode(Qt::ElideRight);
    source_songs_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    source_songs_list_->setAlternatingRowColors(true);
    source_layout->addWidget(source_songs_list_, 3);

    auto* action_panel = new QWidget();
    action_panel->setObjectName("managementActionPanel");
    auto* action_layout = new QVBoxLayout(action_panel);
    action_layout->addStretch();
    copy_button_ = new QPushButton("复制");
    copy_button_->setToolTip("将左侧选中歌曲暂存，以复制到右侧列表");
    move_button_ = new QPushButton("移动");
    move_button_->setToolTip("将左侧选中歌曲移动到右侧列表，并从左侧列表中移除。");
    delete_button_ = new QPushButton("从歌单移除");
    delete_button_->setToolTip("从歌单移除勾选歌曲；不会删除本地文件。应用后保存");
    done_button_ = new QPushButton("应用并返回");
    done_button_->setObjectName("applyButton");
    delete_button_->setObjectName("removeButton");
    done_button_->setToolTip("应用更改并回到播放页面");
    action_layout->addWidget(copy_button_);
    action_layout->addWidget(move_button_);
    action_layout->addWidget(delete_button_);
    action_layout->addSpacing(40);
    action_layout->addWidget(done_button_);
    auto* cancel_button = new QPushButton("取消");
    cancel_button->setObjectName("cancelChangesButton");
    cancel_button->setToolTip("放弃本页未应用的修改，返回播放页面");
    action_layout->addWidget(cancel_button);
    connect(cancel_button, &QPushButton::clicked, this, [this]
    {
        reload();
        emit changes_discarded();
    });
    action_layout->addStretch();
    action_panel->setLayout(action_layout);

    auto* dest_panel = new QWidget();
    dest_panel->setObjectName("managementPanel");
    auto* dest_layout = new QVBoxLayout(dest_panel);
    dest_layout->addWidget(new QLabel("目标歌单"));
    dest_playlists_list_ = new QListWidget();
    dest_playlists_list_->setTextElideMode(Qt::ElideRight);
    dest_playlists_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    dest_playlists_list_->setAlternatingRowColors(true);
    dest_layout->addWidget(dest_playlists_list_, 1);
    dest_layout->addWidget(new QLabel("歌曲"));
    dest_songs_list_ = new QListWidget();
    dest_songs_list_->setTextElideMode(Qt::ElideRight);
    dest_songs_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    dest_songs_list_->setAlternatingRowColors(true);
    dest_layout->addWidget(dest_songs_list_, 3);

    main_layout->addWidget(source_panel, 2);
    main_layout->addWidget(action_panel, 0);
    main_layout->addWidget(dest_panel, 2);

    setLayout(main_layout);
}

void music_management_dialog::setup_connections()
{
    connect(source_playlists_list_, &QListWidget::currentItemChanged, this, &music_management_dialog::on_source_playlist_selected);
    connect(dest_playlists_list_, &QListWidget::currentItemChanged, this, &music_management_dialog::on_dest_playlist_selected);
    connect(source_songs_list_, &QListWidget::itemChanged, this, [this](QListWidgetItem*) { update_source_select_all_button_state(); });

    connect(source_select_all_button_, &QPushButton::clicked, this, &music_management_dialog::on_source_select_all_clicked);
    connect(copy_button_, &QPushButton::clicked, this, &music_management_dialog::on_copy_button_clicked);
    connect(move_button_, &QPushButton::clicked, this, &music_management_dialog::on_move_button_clicked);
    connect(delete_button_, &QPushButton::clicked, this, &music_management_dialog::on_delete_button_clicked);
    connect(done_button_, &QPushButton::clicked, this, &music_management_dialog::on_done_button_clicked);
}

void music_management_dialog::populate_playlist_widgets()
{
    done_button_->setText(has_pending_changes_ ? "应用更改并返回" : "返回播放");
    qint64 current_source_id = -1;
    if (source_playlists_list_->currentItem() != nullptr)
    {
        current_source_id = source_playlists_list_->currentItem()->data(Qt::UserRole).toLongLong();
    }

    qint64 current_dest_id = -1;
    if (dest_playlists_list_->currentItem() != nullptr)
    {
        current_dest_id = dest_playlists_list_->currentItem()->data(Qt::UserRole).toLongLong();
    }

    source_playlists_list_->clear();
    dest_playlists_list_->clear();

    const auto playlists = temp_playlists_.values();
    int source_row_to_select = 0;
    int dest_row_to_select = 0;

    for (int i = 0; i < playlists.count(); ++i)
    {
        const auto& playlist = playlists[i];
        auto* source_item = create_elided_item(QString("%1 [%2]").arg(playlist.name).arg(playlist.songs.count()));
        source_item->setData(Qt::UserRole, playlist.id);
        source_playlists_list_->addItem(source_item);

        auto* dest_item = create_elided_item(QString("%1 [%2]").arg(playlist.name).arg(playlist.songs.count()));
        dest_item->setData(Qt::UserRole, playlist.id);
        dest_playlists_list_->addItem(dest_item);

        if (current_source_id != -1 && playlist.id == current_source_id)
        {
            source_row_to_select = i;
        }
        if (current_dest_id != -1 && playlist.id == current_dest_id)
        {
            dest_row_to_select = i;
        }
    }

    if (source_playlists_list_->count() > 0)
    {
        source_playlists_list_->setCurrentRow(source_row_to_select);
    }
    if (dest_playlists_list_->count() > 0)
    {
        dest_playlists_list_->setCurrentRow(dest_row_to_select);
    }
}

void music_management_dialog::update_songs_list(QListWidget* songs_list_widget, QListWidget* playlists_list_widget, bool is_source_list)
{
    songs_list_widget->clear();
    QListWidgetItem* current_playlist_item = playlists_list_widget->currentItem();
    if (current_playlist_item == nullptr)
    {
        if (is_source_list)
        {
            update_source_select_all_button_state();
        }
        return;
    }

    const qint64 playlist_id = current_playlist_item->data(Qt::UserRole).toLongLong();
    const Playlist& playlist = temp_playlists_.value(playlist_id);

    for (const auto& song : playlist.songs)
    {
        auto* song_item = create_elided_item(song.file_name);
        song_item->setData(Qt::UserRole, song.file_path);
        if (is_source_list)
        {
            song_item->setFlags(song_item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            song_item->setCheckState(Qt::Unchecked);
        }
        else
        {
            song_item->setFlags(Qt::ItemIsEnabled);
        }
        songs_list_widget->addItem(song_item);
    }

    if (is_source_list)
    {
        update_source_select_all_button_state();
    }
}

void music_management_dialog::on_source_playlist_selected() { update_songs_list(source_songs_list_, source_playlists_list_, true); }
void music_management_dialog::on_dest_playlist_selected()
{
    update_songs_list(dest_songs_list_, dest_playlists_list_, false);
    update_source_select_all_button_state();
}

void music_management_dialog::update_source_select_all_button_state()
{
    const int song_count = source_songs_list_->count();
    bool all_checked = song_count > 0;
    bool any_checked = false;
    for (int i = 0; i < song_count; ++i)
    {
        const bool checked = source_songs_list_->item(i)->checkState() == Qt::Checked;
        all_checked = all_checked && checked;
        any_checked = any_checked || checked;
    }
    const auto* source = source_playlists_list_->currentItem();
    const auto* dest = dest_playlists_list_->currentItem();
    const bool can_transfer = any_checked && source != nullptr && dest != nullptr
                              && source->data(Qt::UserRole) != dest->data(Qt::UserRole);
    copy_button_->setEnabled(can_transfer);
    move_button_->setEnabled(can_transfer);
    delete_button_->setEnabled(any_checked);

    source_select_all_button_->setEnabled(song_count > 0);
    source_select_all_button_->setText(all_checked ? "取消全选" : "全选");
    source_select_all_button_->setToolTip(all_checked ? "取消勾选当前源歌单中的所有歌曲" : "勾选当前源歌单中的所有歌曲");
}

void music_management_dialog::on_source_select_all_clicked()
{
    bool all_checked = source_songs_list_->count() > 0;
    for (int i = 0; i < source_songs_list_->count(); ++i)
    {
        if (source_songs_list_->item(i)->checkState() != Qt::Checked)
        {
            all_checked = false;
            break;
        }
    }

    const QSignalBlocker blocker(source_songs_list_);
    for (int i = 0; i < source_songs_list_->count(); ++i)
    {
        source_songs_list_->item(i)->setCheckState(all_checked ? Qt::Unchecked : Qt::Checked);
    }
    update_source_select_all_button_state();
}

void music_management_dialog::on_copy_button_clicked()
{
    QListWidgetItem* source_playlist_item = source_playlists_list_->currentItem();
    QListWidgetItem* dest_playlist_item = dest_playlists_list_->currentItem();
    if (source_playlist_item == nullptr || dest_playlist_item == nullptr)
    {
        return;
    }

    const qint64 source_playlist_id = source_playlist_item->data(Qt::UserRole).toLongLong();
    const qint64 dest_playlist_id = dest_playlist_item->data(Qt::UserRole).toLongLong();
    if (source_playlist_id == dest_playlist_id)
    {
        return;
    }

    Playlist& dest_playlist_ref = temp_playlists_[dest_playlist_id];
    QSet<QString> dest_song_paths;
    for (const auto& song : dest_playlist_ref.songs)
    {
        dest_song_paths.insert(song.file_path);
    }

    QList<Song> songs_to_add;
    for (int i = 0; i < source_songs_list_->count(); ++i)
    {
        QListWidgetItem* song_item = source_songs_list_->item(i);
        if (song_item->checkState() == Qt::Checked)
        {
            const QString song_path = song_item->data(Qt::UserRole).toString();
            if (!dest_song_paths.contains(song_path))
            {
                songs_to_add.append({song_path, song_item->text()});
            }
        }
    }

    if (songs_to_add.isEmpty())
    {
        return;
    }

    dest_playlist_ref.songs.append(songs_to_add);
    has_pending_changes_ = true;

    populate_playlist_widgets();
}

void music_management_dialog::on_move_button_clicked()
{
    QListWidgetItem* source_playlist_item = source_playlists_list_->currentItem();
    QListWidgetItem* dest_playlist_item = dest_playlists_list_->currentItem();
    if (source_playlist_item == nullptr || dest_playlist_item == nullptr)
    {
        QMessageBox::warning(this, "操作无效", "请同时选择源播放列表和目标播放列表。");
        return;
    }

    const qint64 source_playlist_id = source_playlist_item->data(Qt::UserRole).toLongLong();
    const qint64 dest_playlist_id = dest_playlist_item->data(Qt::UserRole).toLongLong();
    if (source_playlist_id == dest_playlist_id)
    {
        QMessageBox::information(this, "操作无效", "源播放列表和目标播放列表不能相同。");
        return;
    }

    Playlist& dest_playlist_ref = temp_playlists_[dest_playlist_id];
    QSet<QString> dest_song_paths;
    for (const auto& song : dest_playlist_ref.songs)
    {
        dest_song_paths.insert(song.file_path);
    }

    QList<Song> songs_to_add;
    QList<int> indices_to_remove;
    for (int i = 0; i < source_songs_list_->count(); ++i)
    {
        QListWidgetItem* song_item = source_songs_list_->item(i);
        if (song_item->checkState() == Qt::Checked)
        {
            indices_to_remove.append(i);
            const QString song_path = song_item->data(Qt::UserRole).toString();
            if (!dest_song_paths.contains(song_path))
            {
                songs_to_add.append({song_path, song_item->text()});
            }
        }
    }

    if (indices_to_remove.isEmpty())
    {
        QMessageBox::information(this, "提示", "请在左侧勾选要移动的歌曲。");
        return;
    }

    if (!songs_to_add.isEmpty())
    {
        dest_playlist_ref.songs.append(songs_to_add);
    }

    Playlist& source_playlist_ref = temp_playlists_[source_playlist_id];
    std::sort(indices_to_remove.begin(), indices_to_remove.end(), std::greater<>());
    for (int index : indices_to_remove)
    {
        source_playlist_ref.songs.removeAt(index);
    }

    has_pending_changes_ = true;
    populate_playlist_widgets();
}

void music_management_dialog::on_delete_button_clicked()
{
    QListWidgetItem* source_playlist_item = source_playlists_list_->currentItem();
    if (source_playlist_item == nullptr)
    {
        return;
    }

    const qint64 playlist_id = source_playlist_item->data(Qt::UserRole).toLongLong();
    Playlist& source_playlist_ref = temp_playlists_[playlist_id];

    QList<int> indices_to_remove;
    for (int i = 0; i < source_songs_list_->count(); ++i)
    {
        if (source_songs_list_->item(i)->checkState() == Qt::Checked)
        {
            indices_to_remove.append(i);
        }
    }
    if (indices_to_remove.isEmpty())
    {
        return;
    }

    std::sort(indices_to_remove.begin(), indices_to_remove.end(), std::greater<>());
    for (int index : indices_to_remove)
    {
        if (index >= 0 && index < source_playlist_ref.songs.count())
        {
            source_playlist_ref.songs.removeAt(index);
        }
    }

    has_pending_changes_ = true;
    populate_playlist_widgets();
}

void music_management_dialog::on_done_button_clicked()
{
    if (!has_pending_changes_)
    {
        emit changes_discarded();
        return;
    }
    if (!playlist_manager_->apply_changes_from_dialog(temp_playlists_))
    {
        QMessageBox::warning(this, "保存失败", "歌单修改未能保存，更改仍保留在本页。请检查磁盘空间和文件访问权限后重试。");
        return;
    }
    has_pending_changes_ = false;
    emit changes_applied();
}
