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
#include <QRandomGenerator>
#include <QFont>
#include <QPixmap>
#include <QCollator>
#include <QListWidget>
#include <QScrollBar>
#include <QEasingCurve>

#include "log.h"
#include "tray_icon.h"
#include "mainwindow.h"
#include "volumemeter.h"
#include "quick_editor.h"
#include "spectrum_widget.h"
#include "playlist_manager.h"
#include "playback_controller.h"
#include "music_management_dialog.h"

constexpr qint64 LYRIC_PREDICTION_OFFSET_MS = 250;

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

    controller_->set_volume(volume_meter_->value());

    controller_->set_spectrum_widget(spectrum_widget_);

    playlist_manager_->load_playlists();
    populate_playlists_on_startup();
    setWindowTitle("Music Player");
    resize(800, 600);
}

mainwindow::~mainwindow() = default;

void mainwindow::quit_application()
{
    playlist_manager_->save_playlists();
    hide();
    QApplication::quit();
}
void mainwindow::closeEvent(QCloseEvent* event)
{
    if (tray_icon_->isVisible())
    {
        hide();
        event->ignore();
    }
    else
    {
        event->accept();
    }
}

void mainwindow::setup_ui()
{
    tray_icon_ = new tray_icon(this);
    connect(tray_icon_, &tray_icon::show_hide_triggered, this, [this]() { isVisible() ? hide() : show(); });
    connect(tray_icon_, &tray_icon::quit_triggered, this, &mainwindow::quit_application);
    tray_icon_->show();

    auto* central_widget = new QWidget(this);
    central_widget->setObjectName("centralWidget");
    setCentralWidget(central_widget);

    auto* main_layout = new QVBoxLayout(central_widget);
    main_layout->setContentsMargins(0, 0, 0, 0);
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
    bottom_container->setFixedHeight(220);

    auto* bottom_h_layout = new QHBoxLayout(bottom_container);
    bottom_h_layout->setContentsMargins(0, 0, 0, 0);
    bottom_h_layout->setSpacing(0);

    auto* left_panel = new QWidget();
    auto* main_grid_layout = new QGridLayout(left_panel);
    main_grid_layout->setContentsMargins(10, 10, 10, 0);
    main_grid_layout->setSpacing(5);
    main_grid_layout->setVerticalSpacing(3);

    auto* top_display_container = new QWidget();
    auto* top_display_layout = new QHBoxLayout(top_display_container);
    top_display_layout->setContentsMargins(0, 0, 0, 0);
    top_display_layout->setSpacing(10);

    cover_art_label_ = new QLabel();
    cover_art_label_->setObjectName("coverArtLabel");
    cover_art_label_->setFixedSize(80, 80);
    cover_art_label_->setScaledContents(true);
    cover_art_label_->setStyleSheet("border: 1px solid #E0E0E0; border-radius: 5px;");
    cover_art_label_->hide();

    spectrum_widget_ = new spectrum_widget(this);

    top_display_layout->addWidget(cover_art_label_);
    top_display_layout->addWidget(spectrum_widget_);

    lyrics_list_widget_ = new QListWidget(this);
    lyrics_list_widget_->setObjectName("lyricsListWidget");
    lyrics_list_widget_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lyrics_list_widget_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    lyrics_list_widget_->setSelectionMode(QAbstractItemView::SingleSelection);
    lyrics_list_widget_->setFocusPolicy(Qt::NoFocus);
    lyrics_list_widget_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lyrics_list_widget_->hide();

    lyrics_scroll_animation_ = new QPropertyAnimation(this);

    progress_slider_ = new QSlider(Qt::Horizontal);

    stop_button_ = new QPushButton(QIcon(":/icons/stop.svg"), "");
    prev_button_ = new QPushButton(QIcon(":/icons/previous.svg"), "");
    play_pause_button_ = new QPushButton(QIcon(":/icons/play.svg"), "");
    next_button_ = new QPushButton(QIcon(":/icons/next.svg"), "");
    shuffle_button_ = new QPushButton(QIcon(":/icons/repeat.svg"), "");
    manage_button_ = new QPushButton(QIcon(":/icons/manage.svg"), "");
    shuffle_button_->setObjectName("modeButton");

    QSize icon_size(24, 24);
    stop_button_->setIconSize(icon_size);
    prev_button_->setIconSize(icon_size);
    play_pause_button_->setIconSize(QSize(28, 28));
    next_button_->setIconSize(icon_size);
    shuffle_button_->setIconSize(icon_size);
    manage_button_->setIconSize(icon_size);

    stop_button_->setToolTip("停止");
    prev_button_->setToolTip("上一首");
    play_pause_button_->setToolTip("播放/暂停");
    next_button_->setToolTip("下一首");
    shuffle_button_->setToolTip("列表循环");
    manage_button_->setToolTip("管理音乐");

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
    left_layout->addWidget(manage_button_);
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

    main_grid_layout->addWidget(top_display_container, 0, 0, 1, 3);
    main_grid_layout->addWidget(lyrics_list_widget_, 1, 0, 1, 3);
    main_grid_layout->addWidget(progress_slider_, 2, 0, 1, 3);

    main_grid_layout->addWidget(all_buttons_container, 3, 1, Qt::AlignCenter);
    main_grid_layout->addWidget(time_label_, 3, 2, Qt::AlignRight | Qt::AlignVCenter);

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

    main_layout->addWidget(bottom_container);
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
    connect(shuffle_button_, &QPushButton::clicked, this, &mainwindow::on_playback_mode_clicked);
    connect(manage_button_, &QPushButton::clicked, this, &mainwindow::on_manage_playlists_action);

    connect(song_tree_widget_, &QTreeWidget::itemDoubleClicked, this, &mainwindow::on_tree_item_double_clicked);
    connect(song_tree_widget_, &QTreeWidget::customContextMenuRequested, this, &mainwindow::on_song_tree_context_menu_requested);

    connect(controller_, &playback_controller::track_info_ready, this, &mainwindow::update_track_info);
    connect(controller_, &playback_controller::playback_started, this, &mainwindow::on_playback_started);
    connect(controller_, &playback_controller::progress_updated, this, &mainwindow::update_progress);
    connect(controller_, &playback_controller::playback_finished, this, &mainwindow::handle_playback_finished);
    connect(controller_, &playback_controller::playback_error, this, &mainwindow::handle_playback_error);
    connect(controller_, &playback_controller::metadata_ready, this, &mainwindow::on_metadata_updated);
    connect(controller_, &playback_controller::cover_art_ready, this, &mainwindow::on_cover_art_updated);
    connect(controller_, &playback_controller::lyrics_updated, this, &mainwindow::on_lyrics_updated);

    connect(playlist_manager_, &playlist_manager::playlist_added, this, &mainwindow::on_playlist_added);
    connect(playlist_manager_, &playlist_manager::playlist_removed, this, &mainwindow::on_playlist_removed);
    connect(playlist_manager_, &playlist_manager::playlist_renamed, this, &mainwindow::on_playlist_renamed);
    connect(playlist_manager_, &playlist_manager::songs_changed_in_playlist, this, &mainwindow::on_songs_changed);
}

void mainwindow::on_stop_clicked()
{
    controller_->stop();
    play_pause_button_->setIcon(QIcon(":/icons/play.svg"));
    is_playing_ = false;
    is_paused_ = false;
    setWindowTitle("Music Player");
    time_label_->setText("00:00 / 00:00");
    progress_slider_->setValue(0);
    shuffled_indices_.clear();
    current_shuffle_index_ = -1;
    clear_playing_indicator();
    cover_art_label_->hide();
    lyrics_list_widget_->hide();
    lyrics_list_widget_->clear();
    current_lyrics_.clear();
}

void mainwindow::on_playback_started(const QString& file_path, const QString& file_name)
{
    lyrics_list_widget_->hide();
    lyrics_list_widget_->clear();
    current_lyrics_.clear();
    current_lyric_index_ = -1;

    is_playing_ = true;
    is_paused_ = false;
    play_pause_button_->setIcon(QIcon(":/icons/pause.svg"));
    setWindowTitle(file_name);
    clear_playing_indicator();

    cover_art_label_->hide();

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

void mainwindow::on_cover_art_updated(const QByteArray& image_data)
{
    QPixmap cover_pixmap;
    if (cover_pixmap.loadFromData(image_data))
    {
        cover_art_label_->setPixmap(cover_pixmap);
        cover_art_label_->show();
    }
    else
    {
        LOG_WARN("无法从数据加载封面图片");
        cover_art_label_->hide();
    }
}

void mainwindow::on_lyrics_updated(const QList<LyricLine>& lyrics)
{
    current_lyrics_ = lyrics;
    current_lyric_index_ = -1;
    lyrics_list_widget_->clear();

    if (current_lyrics_.isEmpty())
    {
        lyrics_list_widget_->hide();
    }
    else
    {
        for (const auto& line : current_lyrics_)
        {
            auto* item = new QListWidgetItem(line.text, lyrics_list_widget_);
            item->setTextAlignment(Qt::AlignCenter);
        }
        lyrics_list_widget_->show();
    }
}

void mainwindow::on_playback_mode_clicked()
{
    switch (current_mode_)
    {
        case playback_mode::ListLoop:
            current_mode_ = playback_mode::SingleLoop;
            break;
        case playback_mode::SingleLoop:
            current_mode_ = playback_mode::Shuffle;
            break;
        case playback_mode::Shuffle:
            current_mode_ = playback_mode::Sequential;
            break;
        case playback_mode::Sequential:
            current_mode_ = playback_mode::ListLoop;
            break;
    }

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

    update_playback_mode_button_style();
}

void mainwindow::generate_shuffled_list(QTreeWidgetItem* playlist_item, int start_song_index)
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
    QStringList sl;
    for (const auto& l : shuffled_indices_)
    {
        sl << QString::number(l);
    }
    auto list_str = sl.join(',');

    if (start_song_index != -1)
    {
        const int current_pos = static_cast<int>(shuffled_indices_.indexOf(start_song_index));
        if (current_pos != -1)
        {
            shuffled_indices_.swapItemsAt(0, current_pos);
        }
    }

    LOG_INFO("已为播放列表 '{}' 生成随机队列，起始歌曲索引: {} {}", playlist_item->text(0).toStdString(), start_song_index, list_str.toStdString());
}

void mainwindow::update_playback_mode_button_style()
{
    switch (current_mode_)
    {
        case playback_mode::ListLoop:
            shuffle_button_->setIcon(QIcon(":/icons/repeat.svg"));
            shuffle_button_->setToolTip("列表循环");
            break;
        case playback_mode::SingleLoop:
            shuffle_button_->setIcon(QIcon(":/icons/repeat-one.svg"));
            shuffle_button_->setToolTip("单曲循环");
            break;
        case playback_mode::Shuffle:
            shuffle_button_->setIcon(QIcon(":/icons/shuffle.svg"));
            shuffle_button_->setToolTip("随机播放");
            break;
        case playback_mode::Sequential:
            shuffle_button_->setIcon(QIcon(":/icons/sequential.svg"));
            shuffle_button_->setToolTip("顺序播放");
            break;
    }
    shuffle_button_->setProperty("active", current_mode_ != playback_mode::ListLoop);
    style()->unpolish(shuffle_button_);
    style()->polish(shuffle_button_);
}

void mainwindow::on_manage_playlists_action()
{
    LOG_INFO("打开音乐管理对话框");
    auto* dialog = new music_management_dialog(playlist_manager_, this);
    dialog->exec();
    LOG_INFO("音乐管理对话框已关闭");
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
        playlist_item->setExpanded(false);

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
    playlist_item->setExpanded(false);
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
        auto* sort_playlist_action = context_menu.addAction("排序列表");
        context_menu.addSeparator();
        auto* rename_action = context_menu.addAction("重命名");
        auto* delete_action = context_menu.addAction("删除播放列表");
        context_menu.addSeparator();
        auto* new_playlist_action = context_menu.addAction("新建播放列表");

        connect(add_songs_action, &QAction::triggered, this, &mainwindow::on_add_songs_action);
        connect(sort_playlist_action, &QAction::triggered, this, &mainwindow::on_sort_playlist_action);
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

    const QStringList files =
        QFileDialog::getOpenFileNames(this, "选择要添加的音乐文件", "", "音频文件 (*.mp3 *.flac *.wav *.m4a *.ogg *.mp4 *.webm)");

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

void mainwindow::on_sort_playlist_action()
{
    if (context_menu_item_ == nullptr || context_menu_item_->parent() != nullptr)
    {
        return;
    }
    const QString playlist_id = context_menu_item_->data(0, Qt::UserRole).toString();
    LOG_INFO("动作触发 排序播放列表 id {}", playlist_id.toStdString());
    playlist_manager_->sort_playlist(playlist_id);
}

void mainwindow::on_tree_item_double_clicked(QTreeWidgetItem* item, int column)
{
    if (item == nullptr || item->parent() == nullptr || column != 0)
    {
        return;
    }
    current_playing_file_path_ = item->data(0, Qt::UserRole).toString();
    clicked_song_item_ = item;

    if (current_mode_ == playback_mode::Shuffle)
    {
        QTreeWidgetItem* playlist_item = item->parent();
        const int clicked_song_index = playlist_item->indexOfChild(item);
        generate_shuffled_list(playlist_item, clicked_song_index);
        current_shuffle_index_ = 0;
    }
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

    if (next_item == nullptr || next_item->parent() == nullptr)
    {
        if (current_mode_ == playback_mode::ListLoop)
        {
            auto* first_playlist = song_tree_widget_->topLevelItem(0);
            if (first_playlist != nullptr && first_playlist->childCount() > 0)
            {
                next_item = first_playlist->child(0);
            }
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

void mainwindow::on_prev_clicked()
{
    if (currently_playing_item_ == nullptr)
    {
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

    if (prev_item == nullptr || prev_item->parent() == nullptr)
    {
        if (current_mode_ == playback_mode::ListLoop)
        {
            auto* last_playlist = song_tree_widget_->topLevelItem(song_tree_widget_->topLevelItemCount() - 1);
            if (last_playlist != nullptr && last_playlist->childCount() > 0)
            {
                prev_item = last_playlist->child(last_playlist->childCount() - 1);
            }
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

void mainwindow::on_volume_changed(int value) { controller_->set_volume(value); }

void mainwindow::update_track_info(qint64 duration_ms)
{
    progress_slider_->setRange(0, static_cast<int>(duration_ms));
    QTime total_time = QTime(0, 0).addMSecs(static_cast<int>(duration_ms));
    QString format = duration_ms >= 3600000 ? "hh:mm:ss" : "mm:ss";
    QString current_time_str = QTime(0, 0).toString(format);
    time_label_->setText(QString("%1 / %2").arg(current_time_str).arg(total_time.toString(format)));
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

    if (current_lyrics_.isEmpty())
    {
        return;
    }

    const qint64 predicted_ms = current_ms + LYRIC_PREDICTION_OFFSET_MS;

    int new_lyric_index = -1;
    for (int i = 0; i < current_lyrics_.size(); ++i)
    {
        if (predicted_ms >= current_lyrics_[i].timestamp_ms)
        {
            new_lyric_index = i;
        }
        else
        {
            break;
        }
    }

    if (new_lyric_index != -1 && new_lyric_index != current_lyric_index_)
    {
        if (lyrics_scroll_animation_->state() == QAbstractAnimation::Running)
        {
            lyrics_scroll_animation_->stop();
        }

        if (current_lyric_index_ >= 0 && current_lyric_index_ < lyrics_list_widget_->count())
        {
            auto* old_item = lyrics_list_widget_->item(current_lyric_index_);
            if (old_item)
                old_item->setSelected(false);
        }

        auto* new_item = lyrics_list_widget_->item(new_lyric_index);
        if (new_item)
        {
            new_item->setSelected(true);

            int start_value = lyrics_list_widget_->verticalScrollBar()->value();
            lyrics_list_widget_->scrollToItem(new_item, QAbstractItemView::PositionAtCenter);
            int end_value = lyrics_list_widget_->verticalScrollBar()->value();
            lyrics_list_widget_->verticalScrollBar()->setValue(start_value);

            lyrics_scroll_animation_->setTargetObject(lyrics_list_widget_->verticalScrollBar());
            lyrics_scroll_animation_->setPropertyName("value");
            lyrics_scroll_animation_->setDuration(300);
            lyrics_scroll_animation_->setStartValue(start_value);
            lyrics_scroll_animation_->setEndValue(end_value);
            lyrics_scroll_animation_->setEasingCurve(QEasingCurve::InOutQuad);
            lyrics_scroll_animation_->start();
        }

        current_lyric_index_ = new_lyric_index;
    }
}

void mainwindow::handle_playback_finished()
{
    is_playing_ = false;
    is_paused_ = false;
    play_pause_button_->setIcon(QIcon(":/icons/play.svg"));

    switch (current_mode_)
    {
        case playback_mode::ListLoop:
        case playback_mode::Shuffle:
            on_next_clicked();
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
                on_stop_clicked();
                return;
            }

            QTreeWidgetItem* playlist_item = currently_playing_item_->parent();
            if (playlist_item != nullptr)
            {
                int current_index = playlist_item->indexOfChild(currently_playing_item_);
                int song_count = playlist_item->childCount();

                if (current_index == song_count - 1)
                {
                    on_stop_clicked();
                }
                else
                {
                    on_next_clicked();
                }
            }
            else
            {
                on_stop_clicked();
            }
            break;
    }
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

void mainwindow::on_metadata_updated(const QMap<QString, QString>& metadata)
{
    QString title = metadata.value("title");
    QString artist = metadata.value("artist");
    QString display_text;

    if (!artist.isEmpty() && !title.isEmpty())
    {
        display_text = QString("%1 - %2").arg(artist, title);
    }
    else if (!title.isEmpty())
    {
        display_text = title;
    }
    else
    {
        return;
    }

    setWindowTitle(display_text);
}
