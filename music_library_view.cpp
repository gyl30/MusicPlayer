#include <QCollator>
#include <QFileInfo>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QSet>
#include <QShortcut>
#include <algorithm>

#include "music_library_view.h"
#include "playlist_manager.h"
#include "song_delegate.h"

music_library_view::music_library_view(playlist_manager* manager, QWidget* parent) : QTreeWidget(parent), manager_(manager)
{
    setObjectName("musicLibraryView");
    setColumnCount(2);
    setItemDelegate(new song_delegate(song_delegate::rows::all_songs, this));
    setUniformRowHeights(true);
    setMouseTracking(true);
    setAllColumnsShowFocus(true);
    setRootIsDecorated(false);
    setIndentation(0);
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
    connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) { emit play_requested(item->data(0, Qt::UserRole).toString()); });
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter})
    {
        connect(new QShortcut(key, this, nullptr, nullptr, Qt::WidgetShortcut), &QShortcut::activated, this, [this]
        {
            if (currentItem() != nullptr) emit play_requested(currentItem()->data(0, Qt::UserRole).toString());
        });
    }
    connect(new QShortcut(QKeySequence::Delete, this, nullptr, nullptr, Qt::WidgetShortcut), &QShortcut::activated, this, &music_library_view::on_delete_action);

    connect(manager_, &playlist_manager::library_changed, this, &music_library_view::refresh_when_visible);
    connect(manager_, &playlist_manager::songs_changed_in_playlist, this, &music_library_view::refresh_when_visible);
    connect(manager_, &playlist_manager::playlist_added, this, &music_library_view::refresh_when_visible);
    connect(manager_, &playlist_manager::playlist_removed, this, &music_library_view::refresh_when_visible);
    connect(manager_, &playlist_manager::playlist_renamed, this, &music_library_view::refresh_when_visible);
    connect(manager_, &playlist_manager::durations_updated, this, &music_library_view::refresh_when_visible);
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

    clear();
    for (const auto& entry : songs)
    {
        auto* item = new QTreeWidgetItem(this);
        const bool available = QFileInfo(entry.song.file_path).isFile();
        item->setText(0, normalize_display_text(entry.song.file_name));
        item->setText(1, available ? format_duration_ms(entry.song.duration_ms) : QStringLiteral("失效"));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setData(0, Qt::UserRole, entry.song.file_path);
        const QString lists = entry.playlists.isEmpty() ? QStringLiteral("未加入歌单") : QString("所在歌单：%1").arg(entry.playlists.join("、"));
        item->setToolTip(0, QString("%1\n%2%3").arg(entry.song.file_path, lists, available ? QString() : QStringLiteral("\n文件不存在或无法访问")));
        item->setSelected(selected.contains(entry.song.file_path));
        if (entry.song.file_path == current) setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
    }
    set_filter(filter_);
}

void music_library_view::set_filter(const QString& keyword)
{
    filter_ = keyword.trimmed();
    for (int i = 0; i < topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* item = topLevelItem(i);
        item->setHidden(!filter_.isEmpty() && !item->text(0).contains(filter_, Qt::CaseInsensitive));
    }
}

QStringList music_library_view::selected_paths() const
{
    QStringList paths;
    for (QTreeWidgetItem* item : selectedItems())
    {
        if (!item->isHidden()) paths.append(item->data(0, Qt::UserRole).toString());
    }
    return paths;
}

void music_library_view::show_context_menu(const QPoint& position)
{
    QTreeWidgetItem* item = itemAt(position);
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
