#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QLabel>
#include <QSet>
#include <algorithm>
#include "music_management_dialog.h"
#include "playlist_manager.h"
#include "log.h"

music_management_dialog::music_management_dialog(playlist_manager* manager, QWidget* parent) : QDialog(parent), playlist_manager_(manager)
{
    setup_ui();
    setup_connections();
    load_initial_data();
    populate_playlist_widgets();
    setWindowTitle("音乐管理");
    resize(900, 600);
}

void music_management_dialog::load_initial_data()
{
    temp_playlists_.clear();
    const auto all_playlists = playlist_manager_->get_all_playlists();
    for (const auto& playlist : all_playlists)
    {
        temp_playlists_.insert(playlist.id, playlist);
    }
    LOG_INFO("音乐管理对话框已创建并加载了 {} 个播放列表的临时副本", temp_playlists_.count());
}

void music_management_dialog::setup_ui()
{
    auto* main_layout = new QHBoxLayout(this);

    auto* source_panel = new QWidget();
    auto* source_layout = new QVBoxLayout(source_panel);
    source_layout->addWidget(new QLabel("源播放列表 (可操作)"));
    source_playlists_list_ = new QListWidget();
    source_layout->addWidget(source_playlists_list_);
    source_layout->addWidget(new QLabel("源歌曲列表"));
    source_songs_list_ = new QListWidget();
    source_layout->addWidget(source_songs_list_);

    auto* action_panel = new QWidget();
    auto* action_layout = new QVBoxLayout(action_panel);
    action_layout->addStretch();
    copy_button_ = new QPushButton("复制 >>");
    copy_button_->setToolTip("将左侧选中歌曲暂存，以复制到右侧列表");
    delete_button_ = new QPushButton("删除");
    delete_button_->setToolTip("将左侧选中歌曲暂存，以待删除");
    done_button_ = new QPushButton("完成");
    done_button_->setToolTip("应用所有更改");
    action_layout->addWidget(copy_button_);
    action_layout->addWidget(delete_button_);
    action_layout->addSpacing(20);
    action_layout->addWidget(done_button_);
    action_layout->addStretch();
    action_panel->setLayout(action_layout);

    auto* dest_panel = new QWidget();
    auto* dest_layout = new QVBoxLayout(dest_panel);
    dest_layout->addWidget(new QLabel("目标播放列表 (仅预览)"));
    dest_playlists_list_ = new QListWidget();
    dest_layout->addWidget(dest_playlists_list_);
    dest_layout->addWidget(new QLabel("目标歌曲列表"));
    dest_songs_list_ = new QListWidget();
    dest_layout->addWidget(dest_songs_list_);

    main_layout->addWidget(source_panel, 2);
    main_layout->addWidget(action_panel, 1);
    main_layout->addWidget(dest_panel, 2);

    setLayout(main_layout);
}

void music_management_dialog::setup_connections()
{
    connect(source_playlists_list_, &QListWidget::currentItemChanged, this, &music_management_dialog::on_source_playlist_selected);
    connect(dest_playlists_list_, &QListWidget::currentItemChanged, this, &music_management_dialog::on_dest_playlist_selected);

    connect(copy_button_, &QPushButton::clicked, this, &music_management_dialog::on_copy_button_clicked);
    connect(delete_button_, &QPushButton::clicked, this, &music_management_dialog::on_delete_button_clicked);
    connect(done_button_, &QPushButton::clicked, this, &music_management_dialog::on_done_button_clicked);
}

void music_management_dialog::populate_playlist_widgets()
{
    QString current_source_id;
    if (source_playlists_list_->currentItem() != nullptr)
    {
        current_source_id = source_playlists_list_->currentItem()->data(Qt::UserRole).toString();
    }

    QString current_dest_id;
    if (dest_playlists_list_->currentItem() != nullptr)
    {
        current_dest_id = dest_playlists_list_->currentItem()->data(Qt::UserRole).toString();
    }

    source_playlists_list_->clear();
    dest_playlists_list_->clear();

    const auto playlists = temp_playlists_.values();
    int source_row_to_select = 0;
    int dest_row_to_select = 0;

    for (int i = 0; i < playlists.count(); ++i)
    {
        const auto& playlist = playlists[i];
        auto* source_item = new QListWidgetItem(QString("%1 [%2]").arg(playlist.name).arg(playlist.songs.count()));
        source_item->setData(Qt::UserRole, playlist.id);
        source_playlists_list_->addItem(source_item);

        auto* dest_item = new QListWidgetItem(QString("%1 [%2]").arg(playlist.name).arg(playlist.songs.count()));
        dest_item->setData(Qt::UserRole, playlist.id);
        dest_playlists_list_->addItem(dest_item);

        if (!current_source_id.isEmpty() && playlist.id == current_source_id)
        {
            source_row_to_select = i;
        }
        if (!current_dest_id.isEmpty() && playlist.id == current_dest_id)
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
        return;
    }

    const QString playlist_id = current_playlist_item->data(Qt::UserRole).toString();
    const Playlist& playlist = temp_playlists_.value(playlist_id);

    for (const auto& song : playlist.songs)
    {
        auto* song_item = new QListWidgetItem(song.fileName);
        song_item->setData(Qt::UserRole, song.filePath);
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
}

void music_management_dialog::on_source_playlist_selected() { update_songs_list(source_songs_list_, source_playlists_list_, true); }
void music_management_dialog::on_dest_playlist_selected() { update_songs_list(dest_songs_list_, dest_playlists_list_, false); }

void music_management_dialog::on_copy_button_clicked()
{
    QListWidgetItem* source_playlist_item = source_playlists_list_->currentItem();
    QListWidgetItem* dest_playlist_item = dest_playlists_list_->currentItem();
    if (source_playlist_item == nullptr || dest_playlist_item == nullptr)
    {
        return;
    }

    const QString source_playlist_id = source_playlist_item->data(Qt::UserRole).toString();
    const QString dest_playlist_id = dest_playlist_item->data(Qt::UserRole).toString();
    if (source_playlist_id == dest_playlist_id)
    {
        return;
    }

    Playlist& dest_playlist_ref = temp_playlists_[dest_playlist_id];
    QSet<QString> dest_song_paths;
    for (const auto& song : dest_playlist_ref.songs)
    {
        dest_song_paths.insert(song.filePath);
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
    LOG_INFO("暂存复制操作: {} 首歌曲到 {}", songs_to_add.count(), dest_playlist_ref.name.toStdString());

    populate_playlist_widgets();
}

void music_management_dialog::on_delete_button_clicked()
{
    QListWidgetItem* source_playlist_item = source_playlists_list_->currentItem();
    if (source_playlist_item == nullptr)
    {
        return;
    }

    const QString playlist_id = source_playlist_item->data(Qt::UserRole).toString();
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
    LOG_INFO("暂存删除操作: 从 {} 移除 {} 首歌曲", source_playlist_ref.name.toStdString(), indices_to_remove.count());

    populate_playlist_widgets();
}

static bool exist_songs(const Playlist& original_playlist, QList<Song> songs)
{
    for (int index = 0; index < songs.count(); index++)
    {
        if (original_playlist.songs[index].filePath != songs[index].filePath)
        {
            return true;
        }
    }
    return false;
}
void music_management_dialog::on_done_button_clicked()
{
    LOG_INFO("用户点击“完成”，开始应用更改...");

    for (const auto& temp_playlist : std::as_const(temp_playlists_))
    {
        Playlist original_playlist = playlist_manager_->get_playlist_by_id(temp_playlist.id);

        if (original_playlist.songs.count() != temp_playlist.songs.count() || exist_songs(original_playlist, temp_playlist.songs))
        {
            LOG_INFO("播放列表 '{}' 有变动，正在应用...", temp_playlist.name.toStdString());

            QList<int> all_indices_to_remove;
            for (int i = 0; i < original_playlist.songs.count(); ++i)
            {
                all_indices_to_remove.append(i);
            }
            if (!all_indices_to_remove.isEmpty())
            {
                playlist_manager_->remove_songs_from_playlist(temp_playlist.id, all_indices_to_remove);
            }

            QStringList new_song_paths;
            for (const auto& song : temp_playlist.songs)
            {
                new_song_paths.append(song.filePath);
            }
            if (!new_song_paths.isEmpty())
            {
                playlist_manager_->add_songs_to_playlist(temp_playlist.id, new_song_paths);
            }
        }
    }

    LOG_INFO("所有更改已应用。");
    accept();
}
