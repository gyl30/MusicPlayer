#include <QCollator>
#include <QFileInfo>
#include <QHeaderView>
#include <QMenu>
#include <QMap>
#include <QMessageBox>
#include <QSettings>
#include <QShortcut>
#include <algorithm>

#include "music_library_view.h"
#include "playlist_manager.h"
#include "song_delegate.h"

// 文件名、标题、歌手、专辑拼成的搜索文本。
constexpr int kSearchTextRole = Qt::UserRole + 2;
constexpr auto kGroupingSettingKey = "library/grouping";

music_library_view::music_library_view(playlist_manager* manager, QWidget* parent) : QTreeWidget(parent), manager_(manager)
{
    setObjectName("musicLibraryView");
    setColumnCount(2);
    delegate_ = new song_delegate(song_delegate::rows::all_songs, this);
    setItemDelegate(delegate_);
    setUniformRowHeights(true);
    setMouseTracking(true);
    setAllColumnsShowFocus(true);
    setRootIsDecorated(false);
    setIndentation(0);
    setExpandsOnDoubleClick(false);
    header()->hide();
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    header()->setSectionResizeMode(1, QHeaderView::Fixed);
    setColumnWidth(1, 64);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setContextMenuPolicy(Qt::CustomContextMenu);

    connect(this, &QTreeWidget::customContextMenuRequested, this, &music_library_view::show_context_menu);
    // 分组节点单击展开或折叠，歌曲双击或回车播放。
    const auto activate = [this](QTreeWidgetItem* item, bool from_click)
    {
        if (item == nullptr) return;
        if (item->childCount() > 0)
        {
            item->setExpanded(!item->isExpanded());
            return;
        }
        if (!from_click) emit play_requested(item->data(0, Qt::UserRole).toString());
    };
    connect(this, &QTreeWidget::itemClicked, this, [activate](QTreeWidgetItem* item, int)
    {
        if (item->childCount() > 0) activate(item, true);
    });
    connect(this, &QTreeWidget::itemDoubleClicked, this, [activate](QTreeWidgetItem* item, int)
    {
        if (item->childCount() == 0) activate(item, false);
    });
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter})
    {
        connect(new QShortcut(key, this, nullptr, nullptr, Qt::WidgetShortcut), &QShortcut::activated, this, [this, activate]
        {
            activate(currentItem(), false);
        });
    }
    connect(this, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) { collapsed_groups_.insert(item->data(0, Qt::UserRole).toString()); });
    connect(this, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) { collapsed_groups_.remove(item->data(0, Qt::UserRole).toString()); });
    connect(new QShortcut(QKeySequence::Delete, this, nullptr, nullptr, Qt::WidgetShortcut), &QShortcut::activated, this, &music_library_view::on_delete_action);

    connect(manager_, &playlist_manager::library_changed, this, &music_library_view::refresh_when_visible);
    connect(manager_, &playlist_manager::songs_changed_in_playlist, this, &music_library_view::refresh_when_visible);
    connect(manager_, &playlist_manager::playlist_added, this, &music_library_view::refresh_when_visible);
    connect(manager_, &playlist_manager::playlist_removed, this, &music_library_view::refresh_when_visible);
    connect(manager_, &playlist_manager::playlist_renamed, this, &music_library_view::refresh_when_visible);
    connect(manager_, &playlist_manager::songs_scanned, this, &music_library_view::refresh_when_visible);

    const QString saved = QSettings("MusicPlayer", "MusicPlayer").value(kGroupingSettingKey).toString();
    grouping_ = saved == "artist" ? grouping::artist : saved == "album" ? grouping::album : grouping::none;
    delegate_->set_layout(grouping_ == grouping::none ? song_delegate::rows::all_songs : song_delegate::rows::nested_in_playlists);
    setIndentation(grouping_ == grouping::none ? 0 : 6);
}

void music_library_view::set_grouping(grouping mode)
{
    if (mode == grouping_)
    {
        return;
    }
    grouping_ = mode;
    collapsed_groups_.clear();
    QSettings("MusicPlayer", "MusicPlayer").setValue(kGroupingSettingKey, mode == grouping::artist ? "artist" : mode == grouping::album ? "album" : "none");
    delegate_->set_layout(mode == grouping::none ? song_delegate::rows::all_songs : song_delegate::rows::nested_in_playlists);
    setIndentation(mode == grouping::none ? 0 : 6);
    refresh();
}

void music_library_view::showEvent(QShowEvent* event)
{
    QTreeWidget::showEvent(event);
    if (dirty_) refresh();
}

void music_library_view::refresh_when_visible()
{
    dirty_ = true;
    if (isVisible()) refresh();
}

void music_library_view::refresh()
{
    dirty_ = false;
    const QStringList selected = selected_paths();
    const QString current = currentItem() != nullptr ? currentItem()->data(0, Qt::UserRole).toString() : QString();

    QList<LibrarySong> songs = manager_->get_library_songs();
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(songs.begin(), songs.end(), [&collator](const LibrarySong& left, const LibrarySong& right)
    {
        return collator.compare(left.song.file_name, right.song.file_name) < 0;
    });

    const QString unknown_group = grouping_ == grouping::artist ? QStringLiteral("未知歌手") : QStringLiteral("未知专辑");
    QMap<QString, QTreeWidgetItem*> groups;
    const auto group_item = [&](const Song& song) -> QTreeWidgetItem*
    {
        if (grouping_ == grouping::none) return nullptr;
        const QString name = grouping_ == grouping::artist ? song.artist : song.album;
        const QString key = name.isEmpty() ? unknown_group : name;
        QTreeWidgetItem*& group = groups[key];
        if (group == nullptr)
        {
            group = new QTreeWidgetItem;
            group->setData(0, Qt::UserRole, key);
            group->setFlags(Qt::ItemIsEnabled);
        }
        return group;
    };

    clear();
    QList<QTreeWidgetItem*> rows;
    for (const auto& entry : songs)
    {
        const Song& song = entry.song;
        auto* item = new QTreeWidgetItem;
        const bool available = QFileInfo(song.file_path).isFile();
        item->setText(0, normalize_display_text(song.file_name));
        item->setText(1, available ? format_duration_ms(song.duration_ms) : QStringLiteral("失效"));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setData(0, Qt::UserRole, song.file_path);
        item->setData(0, kSearchTextRole, QStringList{song.file_name, song.title, song.artist, song.album}.join('\n'));
        QStringList tip{song.file_path};
        if (!song.title.isEmpty()) tip.append(QString("标题：%1").arg(song.title));
        if (!song.artist.isEmpty()) tip.append(QString("歌手：%1").arg(song.artist));
        if (!song.album.isEmpty()) tip.append(QString("专辑：%1").arg(song.album));
        tip.append(entry.playlists.isEmpty() ? QStringLiteral("未加入歌单") : QString("所在歌单：%1").arg(entry.playlists.join("、")));
        if (!available) tip.append(QStringLiteral("文件不存在或无法访问"));
        item->setToolTip(0, tip.join('\n'));
        if (QTreeWidgetItem* group = group_item(song))
        {
            group->addChild(item);
        }
        else
        {
            rows.append(item);
        }
    }

    if (grouping_ != grouping::none)
    {
        QStringList names = groups.keys();
        std::sort(names.begin(), names.end(), [&](const QString& left, const QString& right)
        {
            // 未知分组放在最后。
            if ((left == unknown_group) != (right == unknown_group)) return right == unknown_group;
            return collator.compare(left, right) < 0;
        });
        for (const QString& name : names)
        {
            QTreeWidgetItem* group = groups.value(name);
            group->setText(0, QString("%1 [%2]").arg(name).arg(group->childCount()));
            rows.append(group);
        }
    }
    addTopLevelItems(rows);

    for (int i = 0; i < topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* top = topLevelItem(i);
        if (top->childCount() > 0) top->setExpanded(!collapsed_groups_.contains(top->data(0, Qt::UserRole).toString()));
        const auto restore = [&](QTreeWidgetItem* item)
        {
            const QString path = item->data(0, Qt::UserRole).toString();
            item->setSelected(selected.contains(path));
            if (path == current) setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
        };
        if (top->childCount() == 0) restore(top);
        for (int j = 0; j < top->childCount(); ++j) restore(top->child(j));
    }
    set_filter(filter_);
}

void music_library_view::set_filter(const QString& keyword)
{
    filter_ = keyword.trimmed();
    const auto matches = [this](const QTreeWidgetItem* item)
    {
        return filter_.isEmpty() || item->data(0, kSearchTextRole).toString().contains(filter_, Qt::CaseInsensitive);
    };
    for (int i = 0; i < topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* top = topLevelItem(i);
        if (top->childCount() == 0)
        {
            top->setHidden(!matches(top));
            continue;
        }
        bool any_visible = false;
        for (int j = 0; j < top->childCount(); ++j)
        {
            const bool visible = matches(top->child(j));
            top->child(j)->setHidden(!visible);
            any_visible = any_visible || visible;
        }
        top->setHidden(!any_visible);
    }
}

void music_library_view::focus_first_match()
{
    for (int i = 0; i < topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* top = topLevelItem(i);
        if (top->isHidden()) continue;
        QTreeWidgetItem* song = top;
        for (int j = 0; song == top && j < top->childCount(); ++j)
        {
            if (!top->child(j)->isHidden()) song = top->child(j);
        }
        if (song->childCount() > 0) continue;
        if (song->parent() != nullptr) song->parent()->setExpanded(true);
        setCurrentItem(song);
        setFocus();
        return;
    }
}

QStringList music_library_view::selected_paths() const
{
    QStringList paths;
    for (QTreeWidgetItem* item : selectedItems())
    {
        // 分组节点不可选；折叠分组中的歌曲看不到，不参与批量操作。
        const bool collapsed = item->parent() != nullptr && !item->parent()->isExpanded();
        if (!item->isHidden() && !collapsed && item->childCount() == 0) paths.append(item->data(0, Qt::UserRole).toString());
    }
    return paths;
}

void music_library_view::show_context_menu(const QPoint& position)
{
    QTreeWidgetItem* item = itemAt(position);
    if (item != nullptr && item->childCount() > 0)
    {
        item = nullptr;
    }
    if (item != nullptr && !item->isSelected())
    {
        setCurrentItem(item);
    }

    QMenu menu(this);
    if (item != nullptr)
    {
        QMenu* add_menu = menu.addMenu("添加到歌单");
        for (const auto& playlist : manager_->get_all_playlists())
        {
            const qint64 id = playlist.id;
            add_menu->addAction(playlist.name, this, [this, id] { add_selected_to_playlist(id); });
        }
        add_menu->setEnabled(!add_menu->isEmpty());
        menu.addAction("从音乐库删除", this, &music_library_view::on_delete_action);
        menu.addSeparator();
    }
    QMenu* grouping_menu = menu.addMenu("分组方式");
    for (const auto& [mode, label] : {std::pair{grouping::none, "不分组"}, std::pair{grouping::artist, "按歌手"}, std::pair{grouping::album, "按专辑"}})
    {
        QAction* action = grouping_menu->addAction(label, this, [this, mode = mode] { set_grouping(mode); });
        action->setCheckable(true);
        action->setChecked(grouping_ == mode);
    }
    menu.addAction("清理失效文件", this, &music_library_view::on_cleanup_invalid_action);
    menu.exec(viewport()->mapToGlobal(position));
}

void music_library_view::add_selected_to_playlist(qint64 playlist_id)
{
    const QStringList paths = selected_paths();
    if (paths.isEmpty())
    {
        return;
    }
    QSet<QString> existing;
    for (const auto& song : manager_->get_playlist_by_id(playlist_id).songs)
    {
        existing.insert(song.file_path);
    }
    QStringList to_add;
    for (const QString& path : paths)
    {
        if (!existing.contains(path)) to_add.append(path);
    }
    if (to_add.isEmpty())
    {
        QMessageBox::information(this, "已在歌单中", "所选歌曲都已在该歌单中。");
        return;
    }
    if (!manager_->add_songs_to_playlist(playlist_id, to_add))
    {
        QMessageBox::warning(this, "保存失败", "歌单修改未能保存，请检查磁盘空间和文件访问权限后重试。");
    }
}

void music_library_view::on_delete_action()
{
    const QStringList paths = selected_paths();
    if (paths.isEmpty())
    {
        return;
    }
    const auto answer = QMessageBox::question(this, "从音乐库删除",
        QString("从音乐库删除所选的 %1 首歌曲？\n这些歌曲也会从所有歌单中移除，音乐文件本身不会被删除。").arg(paths.size()));
    if (answer != QMessageBox::Yes)
    {
        return;
    }
    if (!manager_->delete_songs_from_library(paths))
    {
        QMessageBox::warning(this, "保存失败", "音乐库修改未能保存，请检查磁盘空间和文件访问权限后重试。");
    }
}

void music_library_view::on_cleanup_invalid_action()
{
    QStringList missing;
    for (const auto& entry : manager_->get_library_songs())
    {
        if (!QFileInfo(entry.song.file_path).isFile()) missing.append(entry.song.file_path);
    }
    if (missing.isEmpty())
    {
        QMessageBox::information(this, "清理失效文件", "音乐库中没有失效的文件。");
        return;
    }
    const auto answer = QMessageBox::question(this, "清理失效文件",
        QString("音乐库中有 %1 个文件已不存在或无法访问，是否从音乐库和所有歌单中移除？").arg(missing.size()));
    if (answer != QMessageBox::Yes)
    {
        return;
    }
    if (!manager_->delete_songs_from_library(missing))
    {
        QMessageBox::warning(this, "保存失败", "音乐库修改未能保存，请检查磁盘空间和文件访问权限后重试。");
    }
}
