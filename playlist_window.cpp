#include <QApplication>
#include <QByteArray>
#include <QCloseEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QMessageBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QMenu>
#include <QAction>
#include <QRandomGenerator>
#include <QFont>
#include <QCollator>
#include <QShortcut>
#include <QKeySequence>
#include <QTimer>
#include <QSettings>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>
#include <QWindow>
#include <QSize>
#include <QSet>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include "log.h"
#include "tray_icon.h"
#include "quick_editor.h"
#include "playlist_window.h"
#include "playlist_manager.h"
#include "playback_controller.h"
#include "music_management_dialog.h"
#include "lyrics_widget.h"

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

static QString normalize_display_text(const QString& text)
{
    QString display_text = text.trimmed();
    display_text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    display_text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    display_text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    display_text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    return display_text;
}

static void set_item_text_with_tooltip(QTreeWidgetItem* item, const QString& text)
{
    if (item == nullptr)
    {
        return;
    }

    const QString display_text = normalize_display_text(text);
    item->setText(0, display_text);
    item->setToolTip(0, display_text);
}

static void set_song_item_text_with_tooltip(QTreeWidgetItem* item, const QString& file_name)
{
    if (item == nullptr)
    {
        return;
    }

    const QString display_text = normalize_display_text(file_name);
    item->setText(0, display_text);
    item->setToolTip(0, display_text);
}

static qint64 probe_audio_duration_ms(const QString& file_path)
{
    AVFormatContext* format_ctx = nullptr;
    const QByteArray path_bytes = file_path.toUtf8();
    const int open_result = avformat_open_input(&format_ctx, path_bytes.constData(), nullptr, nullptr);
    if (open_result < 0)
    {
        LOG_WARN("播放状态调试 恢复状态探测时长失败 file {} error {}", file_path.toStdString(), open_result);
        return 0;
    }

    const int stream_result = avformat_find_stream_info(format_ctx, nullptr);
    if (stream_result < 0)
    {
        LOG_WARN("播放状态调试 恢复状态读取流信息失败 file {} error {}", file_path.toStdString(), stream_result);
    }

    qint64 duration_ms = 0;
    if (format_ctx->duration != AV_NOPTS_VALUE)
    {
        duration_ms = qMax<qint64>(0, static_cast<qint64>(format_ctx->duration / (AV_TIME_BASE / 1000)));
    }
    avformat_close_input(&format_ctx);
    return duration_ms;
}

static QString song_item_path(QTreeWidgetItem* item)
{
    if (item == nullptr || item->parent() == nullptr)
    {
        return {};
    }

    return item->data(0, Qt::UserRole).toString();
}

static QTreeWidgetItem* find_song_child_by_path(QTreeWidgetItem* playlist_item, const QString& file_path)
{
    if (playlist_item == nullptr || file_path.isEmpty())
    {
        return nullptr;
    }

    for (int i = 0; i < playlist_item->childCount(); ++i)
    {
        QTreeWidgetItem* song_item = playlist_item->child(i);
        if (song_item != nullptr && song_item->data(0, Qt::UserRole).toString() == file_path)
        {
            return song_item;
        }
    }

    return nullptr;
}

constexpr int kPlaybackPageIndex = 0;
constexpr int kManagementPageIndex = 1;

playlist_window::playlist_window(QWidget* parent) : QMainWindow(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);

    controller_ = new playback_controller(this);
    playlist_manager_ = new playlist_manager(this);
    player_window_ = new player_window(controller_, this);
    management_page_ = new music_management_dialog(playlist_manager_, this);

    setup_ui();
    setup_connections();

    playlist_manager_->initialize_and_load();
    populate_playlists_on_startup();
    restore_playback_state();
    setWindowTitle("音乐播放器");
    resize(298, 450);
}

playlist_window::~playlist_window()
{
    save_playback_state();
    if (player_window_ != nullptr)
    {
        player_window_->close();
    }
}

void playlist_window::quit_application()
{
    hide();
    QApplication::quit();
}

void playlist_window::closeEvent(QCloseEvent* event)
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

bool playlist_window::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != nullptr && watched->objectName() == QStringLiteral("appTitleBar") && event->type() == QEvent::MouseButtonPress)
    {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() == Qt::LeftButton && windowHandle() != nullptr)
        {
            windowHandle()->startSystemMove();
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void playlist_window::setup_ui()
{
    tray_icon_ = new tray_icon(this);
    tray_icon_->show();

    auto* central_widget = new QWidget(this);
    central_widget->setObjectName("appFrame");
    setCentralWidget(central_widget);

    auto* main_layout = new QVBoxLayout(central_widget);
    main_layout->setContentsMargins(1, 1, 1, 1);
    main_layout->setSpacing(0);

    auto* title_bar = new QWidget(central_widget);
    title_bar->setObjectName("appTitleBar");
    title_bar->setFixedHeight(22);
    title_bar->installEventFilter(this);

    auto* title_layout = new QHBoxLayout(title_bar);
    title_layout->setContentsMargins(4, 0, 2, 0);
    title_layout->setSpacing(3);

    auto* icon_label = new QLabel(title_bar);
    icon_label->setObjectName("appTitleIcon");
    icon_label->setPixmap(QIcon(":/icons/app_icon.svg").pixmap(16, 16));
    icon_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    title_layout->addWidget(icon_label);

    auto* title_label = new QLabel("音乐播放器", title_bar);
    title_label->setObjectName("appTitleText");
    title_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    title_layout->addWidget(title_label);
    title_layout->addStretch();

    auto* minimize_button = new QPushButton("-", title_bar);
    minimize_button->setObjectName("windowMinButton");
    minimize_button->setFocusPolicy(Qt::NoFocus);
    title_layout->addWidget(minimize_button);

    auto* close_button = new QPushButton("×", title_bar);
    close_button->setObjectName("windowCloseButton");
    close_button->setFocusPolicy(Qt::NoFocus);
    title_layout->addWidget(close_button);

    main_layout->addWidget(title_bar);

    connect(minimize_button, &QPushButton::clicked, this, &playlist_window::showMinimized);
    connect(close_button, &QPushButton::clicked, this, &playlist_window::close);

    song_tree_widget_ = new QTreeWidget();
    song_tree_widget_->setObjectName("songTreeWidget");
    song_tree_widget_->setColumnCount(1);
    song_tree_widget_->header()->hide();
    song_tree_widget_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    song_tree_widget_->setIndentation(0);
    song_tree_widget_->setIconSize(QSize(14, 14));
    song_tree_widget_->setContextMenuPolicy(Qt::CustomContextMenu);
    song_tree_widget_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    song_tree_widget_->setFrameShape(QFrame::NoFrame);
    song_tree_widget_->setTextElideMode(Qt::ElideRight);
    song_tree_widget_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    song_tree_widget_->setAlternatingRowColors(false);

    auto* playback_page = new QWidget(this);
    auto* playback_layout = new QVBoxLayout(playback_page);
    playback_layout->setContentsMargins(0, 0, 0, 0);
    playback_layout->setSpacing(4);
    playback_layout->addWidget(player_window_);
    lyrics_view_ = new lyrics_widget(playback_page);
    lyrics_view_->setObjectName("lyricsView");
    lyrics_view_->setFixedHeight(84);
    lyrics_view_->hide();
    playback_layout->addWidget(lyrics_view_);
    playback_layout->addWidget(song_tree_widget_, 1);

    main_stack_ = new QStackedWidget(this);
    main_stack_->addWidget(playback_page);
    main_stack_->addWidget(management_page_);

    main_layout->addWidget(main_stack_, 1);

    auto* manage_shortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+M")), this);
    connect(manage_shortcut, &QShortcut::activated, this, &playlist_window::on_manage_playlists_action);

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

    connect(playlist_manager_, &playlist_manager::playlist_added, this, &playlist_window::on_playlist_added);
    connect(playlist_manager_, &playlist_manager::playlist_removed, this, &playlist_window::on_playlist_removed);
    connect(playlist_manager_, &playlist_manager::playlist_renamed, this, &playlist_window::on_playlist_renamed);
    connect(playlist_manager_, &playlist_manager::songs_changed_in_playlist, this, &playlist_window::on_songs_changed);

    connect(controller_, &playback_controller::playback_started, this, &playlist_window::on_playback_started);
    connect(controller_, &playback_controller::playback_finished, this, &playlist_window::handle_playback_finished);
    connect(controller_, &playback_controller::playback_error, this, &playlist_window::handle_playback_error_strategy);
    connect(controller_,
            &playback_controller::track_info_ready,
            this,
            [this](qint64 duration_ms)
            {
                current_duration_ms_ = qMax<qint64>(0, duration_ms);
                LOG_INFO("播放状态调试 UI收到时长信息 duration {}ms 当前内存进度 {}ms", current_duration_ms_, current_progress_ms_);
            });
    connect(controller_,
            &playback_controller::progress_updated,
            this,
            [this](qint64 current_ms, qint64 total_ms)
            {
                current_progress_ms_ = current_ms;
                current_duration_ms_ = qMax<qint64>(0, total_ms);
                if (last_logged_progress_ms_ < 0 || current_ms < last_logged_progress_ms_ || current_ms - last_logged_progress_ms_ >= 5000)
                {
                    LOG_INFO("播放状态调试 UI收到进度更新 current {}ms total {}ms 当前内存进度 {}ms 当前内存时长 {}ms",
                             current_ms,
                             total_ms,
                             current_progress_ms_,
                             current_duration_ms_);
                    last_logged_progress_ms_ = current_ms;
                }
                if (lyrics_view_ != nullptr && lyrics_view_->isVisible())
                {
                    lyrics_view_->set_current_time(current_ms);
                }
            });
    connect(controller_,
            &playback_controller::seek_completed,
            this,
            [this](qint64 actual_ms)
            {
                current_progress_ms_ = qMax<qint64>(0, actual_ms);
                last_logged_progress_ms_ = current_progress_ms_;
                LOG_INFO("播放状态调试 UI收到跳转完成 actual {}ms 当前内存进度 {}ms 当前内存时长 {}ms",
                         actual_ms,
                         current_progress_ms_,
                         current_duration_ms_);
            });
    connect(controller_,
            &playback_controller::lyrics_updated,
            this,
            [this](const QList<LyricLine>& lyrics)
            {
                if (lyrics_view_ == nullptr)
                {
                    return;
                }

                lyrics_view_->set_lyrics(lyrics);
                lyrics_view_->setVisible(!lyrics.isEmpty());
            });

    connect(player_window_, &player_window::next_requested, this, &playlist_window::on_next_requested);
    connect(player_window_, &player_window::previous_requested, this, &playlist_window::on_previous_requested);
    connect(player_window_, &player_window::stop_requested, this, &playlist_window::on_stop_requested);
    connect(player_window_, &player_window::play_requested, this, &playlist_window::on_play_requested);
    connect(player_window_, &player_window::playback_mode_changed, this, &playlist_window::on_playback_mode_changed);

    connect(management_page_,
            &music_management_dialog::changes_applied,
            this,
            [this]()
            {
                populate_playlists_on_startup();
                switch_to_page(kPlaybackPageIndex);
            });
}

void playlist_window::switch_to_page(int page_index)
{
    if (main_stack_ == nullptr)
    {
        return;
    }

    main_stack_->setCurrentIndex(page_index);
    resize(page_index == kPlaybackPageIndex ? QSize(298, 450) : QSize(760, 450));
}

void playlist_window::on_playback_mode_changed(playback_mode new_mode)
{
    current_mode_ = new_mode;
    if (current_mode_ == playback_mode::Shuffle && currently_playing_item_ != nullptr)
    {
        QTreeWidgetItem* playlist_item = currently_playing_item_->parent();
        if (playlist_item != nullptr)
        {
            int current_song_index = playlist_item->indexOfChild(currently_playing_item_);
            generate_shuffled_list(playlist_item, current_song_index);
            current_shuffle_index_ = 0;
        }
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
    restored_song_item_ = nullptr;
    current_playing_file_path_.clear();
    current_playing_playlist_id_ = -1;
    current_progress_ms_ = 0;
    current_duration_ms_ = 0;
    pending_restore_seek_ms_ = -1;
    shuffled_indices_.clear();
    current_shuffle_index_ = -1;
    player_window_->on_playback_stopped();
    if (lyrics_view_ != nullptr)
    {
        lyrics_view_->clear();
        lyrics_view_->hide();
    }
}

void playlist_window::on_play_requested()
{
    LOG_INFO("播放状态调试 UI收到播放按钮请求 restored {} current {} current_progress {}ms file {}",
             restored_song_item_ != nullptr,
             currently_playing_item_ != nullptr,
             current_progress_ms_,
             current_playing_file_path_.toStdString());
    if (restored_song_item_ != nullptr)
    {
        LOG_INFO("播放状态调试 走恢复歌曲路径 title {} restore_position {}ms",
                 restored_song_item_->text(0).toStdString(),
                 current_progress_ms_);
        play_song_item(restored_song_item_, true, current_progress_ms_);
        return;
    }

    if (currently_playing_item_ != nullptr)
    {
        LOG_INFO("播放状态调试 走当前歌曲路径 title {} restore_position {}ms",
                 currently_playing_item_->text(0).toStdString(),
                 current_progress_ms_);
        play_song_item(currently_playing_item_, true, current_progress_ms_);
        return;
    }

    LOG_INFO("播放状态调试 无恢复歌曲和当前歌曲 尝试播放第一个可用歌曲");
    if (!play_first_song_in_list())
    {
        QMessageBox::information(this, "没有可播放歌曲", "请先在播放列表中添加歌曲。");
    }
}

void playlist_window::on_playback_started(const QString& file_path, const QString& file_name)
{
    (void)file_path;
    (void)file_name;

    LOG_INFO("播放状态调试 UI收到播放开始 pending_restore_seek {}ms current_progress {}ms",
             pending_restore_seek_ms_,
             current_progress_ms_);
    consecutive_failures_ = 0;
    if (pending_restore_seek_ms_ <= 0)
    {
        current_progress_ms_ = 0;
    }
    pending_restore_seek_ms_ = -1;
    last_logged_progress_ms_ = -1;

    clear_playing_indicator();
    if (lyrics_view_ != nullptr)
    {
        lyrics_view_->clear();
        lyrics_view_->hide();
    }

    if (clicked_song_item_ != nullptr && clicked_song_item_->data(0, Qt::UserRole).toString() == current_playing_file_path_)
    {
        currently_playing_item_ = clicked_song_item_;
        QFont font = currently_playing_item_->font(0);
        font.setBold(true);
        currently_playing_item_->setFont(0, font);
        currently_playing_item_->setForeground(0, QBrush(QColor("#3498DB")));
        currently_playing_item_->setBackground(0, QBrush(QColor("#A6E4FF")));
        if (currently_playing_item_->parent() != nullptr)
        {
            current_playing_playlist_id_ = currently_playing_item_->parent()->data(0, Qt::UserRole).toLongLong();
            currently_playing_item_->parent()->setExpanded(true);
        }
        song_tree_widget_->clearSelection();
        song_tree_widget_->setCurrentItem(currently_playing_item_);
        currently_playing_item_->setSelected(true);
        song_tree_widget_->scrollToItem(currently_playing_item_, QAbstractItemView::PositionAtCenter);
    }
}

void playlist_window::handle_playback_error_strategy(const QString& error_message)
{
    LOG_WARN("播放出错 (连续第 {} 次): {}", consecutive_failures_ + 1, error_message.toStdString());

    consecutive_failures_++;

    if (consecutive_failures_ >= MAX_CONSECUTIVE_FAILURES)
    {
        LOG_ERROR("达到最大连续错误次数 ({})，停止自动播放。", MAX_CONSECUTIVE_FAILURES);
        on_stop_requested();

        QMessageBox::warning(
            this, "播放停止", QString("连续 %1 首歌曲播放失败，已停止播放。\n最后错误: %2").arg(MAX_CONSECUTIVE_FAILURES).arg(error_message));

        consecutive_failures_ = 0;
        return;
    }

    LOG_INFO("尝试自动跳转到下一首...");

    QTimer::singleShot(200, this, [this]() { this->on_next_requested(); });
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

void playlist_window::restore_playback_state()
{
    QSettings settings("MusicPlayer", "MusicPlayer");

    const QString file_path = settings.value("playback/filePath").toString();
    const qint64 position_ms = qMax<qint64>(0, settings.value("playback/positionMs", 0).toLongLong());
    qint64 duration_ms = qMax<qint64>(0, settings.value("playback/durationMs", 0).toLongLong());
    const int saved_mode = settings.value("playback/mode", static_cast<int>(playback_mode::ListLoop)).toInt();
    LOG_INFO("播放状态调试 恢复状态读取 配置文件 {} file {} position {}ms duration {}ms mode {}",
             settings.fileName().toStdString(),
             file_path.toStdString(),
             position_ms,
             duration_ms,
             saved_mode);
    const int min_mode = static_cast<int>(playback_mode::ListLoop);
    const int max_mode = static_cast<int>(playback_mode::Sequential);
    if (saved_mode >= min_mode && saved_mode <= max_mode)
    {
        player_window_->set_playback_mode(static_cast<playback_mode>(saved_mode));
    }

    if (file_path.isEmpty())
    {
        LOG_INFO("播放状态调试 未恢复播放状态 原因 filePath 为空");
        return;
    }

    QTreeWidgetItem* song_item = find_song_item_by_path(file_path);
    if (song_item == nullptr)
    {
        LOG_INFO("播放状态调试 未恢复播放状态 原因 找不到歌曲 file {}", file_path.toStdString());
        return;
    }

    if (duration_ms <= 0)
    {
        duration_ms = probe_audio_duration_ms(file_path);
        LOG_INFO("播放状态调试 恢复状态补充时长 file {} duration {}ms", file_path.toStdString(), duration_ms);
    }

    current_playing_file_path_ = file_path;
    current_progress_ms_ = position_ms;
    current_duration_ms_ = duration_ms;
    pending_restore_seek_ms_ = -1;
    restored_song_item_ = song_item;
    clicked_song_item_ = song_item;
    mark_restored_song_item(song_item);
    player_window_->restore_idle_state(song_item->text(0), position_ms, duration_ms);
    LOG_INFO("播放状态调试 已标记恢复歌曲 title {} position {}ms duration {}ms", song_item->text(0).toStdString(), position_ms, duration_ms);
}

void playlist_window::save_playback_state() const
{
    QSettings settings("MusicPlayer", "MusicPlayer");
    settings.setValue("playback/mode", static_cast<int>(current_mode_));

    if (current_playing_file_path_.isEmpty())
    {
        LOG_INFO("播放状态调试 清理保存状态 配置文件 {} mode {} 原因 current file 为空",
                 settings.fileName().toStdString(),
                 static_cast<int>(current_mode_));
        settings.remove("playback/filePath");
        settings.remove("playback/positionMs");
        settings.remove("playback/durationMs");
        settings.sync();
        return;
    }

    settings.setValue("playback/filePath", current_playing_file_path_);
    settings.setValue("playback/positionMs", current_progress_ms_);
    settings.setValue("playback/durationMs", current_duration_ms_);
    settings.sync();
    LOG_INFO("播放状态调试 保存播放状态 配置文件 {} file {} position {}ms duration {}ms mode {}",
             settings.fileName().toStdString(),
             current_playing_file_path_.toStdString(),
             current_progress_ms_,
             current_duration_ms_,
             static_cast<int>(current_mode_));
}

QTreeWidgetItem* playlist_window::find_song_item_by_path(const QString& file_path) const
{
    if (song_tree_widget_ == nullptr || file_path.isEmpty())
    {
        return nullptr;
    }

    for (int i = 0; i < song_tree_widget_->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* playlist_item = song_tree_widget_->topLevelItem(i);
        if (playlist_item == nullptr)
        {
            continue;
        }

        for (int j = 0; j < playlist_item->childCount(); ++j)
        {
            QTreeWidgetItem* song_item = playlist_item->child(j);
            if (song_item != nullptr && song_item->data(0, Qt::UserRole).toString() == file_path)
            {
                return song_item;
            }
        }
    }

    return nullptr;
}

void playlist_window::mark_restored_song_item(QTreeWidgetItem* item)
{
    if (item == nullptr || item->parent() == nullptr || song_tree_widget_ == nullptr)
    {
        return;
    }

    clear_playing_indicator();
    currently_playing_item_ = item;
    current_playing_playlist_id_ = item->parent()->data(0, Qt::UserRole).toLongLong();

    QFont font = currently_playing_item_->font(0);
    font.setBold(true);
    currently_playing_item_->setFont(0, font);
    currently_playing_item_->setForeground(0, QBrush(QColor("#3498DB")));
    currently_playing_item_->setBackground(0, QBrush(QColor("#A6E4FF")));
    currently_playing_item_->parent()->setExpanded(true);
    song_tree_widget_->clearSelection();
    song_tree_widget_->setCurrentItem(currently_playing_item_);
    currently_playing_item_->setSelected(true);
    song_tree_widget_->scrollToItem(currently_playing_item_, QAbstractItemView::PositionAtCenter);
}

void playlist_window::play_song_item(QTreeWidgetItem* item, bool increment_play_count, qint64 restore_position_ms)
{
    if (item == nullptr || item->parent() == nullptr)
    {
        return;
    }

    current_playing_file_path_ = item->data(0, Qt::UserRole).toString();
    current_playing_playlist_id_ = item->parent()->data(0, Qt::UserRole).toLongLong();
    clicked_song_item_ = item;
    restored_song_item_ = nullptr;
    current_progress_ms_ = restore_position_ms > 0 ? restore_position_ms : 0;
    pending_restore_seek_ms_ = restore_position_ms > 0 ? restore_position_ms : -1;
    last_logged_progress_ms_ = -1;

    if (increment_play_count)
    {
        playlist_manager_->increment_play_count(current_playing_file_path_);
    }

    if (current_mode_ == playback_mode::Shuffle)
    {
        QTreeWidgetItem* playlist_item = item->parent();
        const int clicked_song_index = playlist_item->indexOfChild(item);
        generate_shuffled_list(playlist_item, clicked_song_index);
        current_shuffle_index_ = 0;
    }

    const qint64 start_position_ms = restore_position_ms > 0 ? restore_position_ms : 0;
    LOG_INFO("播放状态调试 准备播放歌曲 title {} file {} restore_position {}ms start_position {}ms pending_restore {}ms",
             item->text(0).toStdString(),
             current_playing_file_path_.toStdString(),
             restore_position_ms,
             start_position_ms,
             pending_restore_seek_ms_);
    controller_->play_file(current_playing_file_path_, start_position_ms);
}

void playlist_window::on_manage_playlists_action()
{
    if (main_stack_ != nullptr && main_stack_->currentIndex() == kManagementPageIndex)
    {
        if (management_page_ != nullptr && management_page_->has_pending_changes())
        {
            QMessageBox::information(this, "未应用更改", "请先点击“应用并返回”保存播放列表更改。");
            return;
        }

        switch_to_page(kPlaybackPageIndex);
        return;
    }

    if (management_page_ != nullptr)
    {
        management_page_->reload();
    }
    switch_to_page(kManagementPageIndex);
}

void playlist_window::populate_playlists_on_startup()
{
    const QString current_file_path = current_playing_file_path_;
    const QString restored_file_path = song_item_path(restored_song_item_);
    const bool had_restored_item = !restored_file_path.isEmpty();

    clear_playing_indicator();
    context_menu_item_ = nullptr;
    clicked_song_item_ = nullptr;
    restored_song_item_ = nullptr;

    song_tree_widget_->blockSignals(true);
    song_tree_widget_->clear();
    QList<Playlist> playlists = playlist_manager_->get_all_playlists();
    for (const auto& playlist : playlists)
    {
        Playlist full_playlist = playlist_manager_->get_playlist_by_id(playlist.id);
        auto* playlist_item = new QTreeWidgetItem(song_tree_widget_);
        set_item_text_with_tooltip(playlist_item, QString("%1 [%2]").arg(playlist.name).arg(full_playlist.songs.count()));
        playlist_item->setData(0, Qt::UserRole, playlist.id);
        playlist_item->setIcon(0, QIcon(":/icons/playlist.svg"));
        playlist_item->setExpanded(false);

        for (const auto& song : full_playlist.songs)
        {
            auto* song_item = new QTreeWidgetItem(playlist_item);
            song_item->setIcon(0, QIcon(":/icons/song.svg"));
            set_song_item_text_with_tooltip(song_item, song.file_name);
            song_item->setData(0, Qt::UserRole, song.file_path);
        }
    }
    song_tree_widget_->blockSignals(false);

    if (current_file_path.isEmpty())
    {
        return;
    }

    QTreeWidgetItem* current_item = find_song_item_by_path(current_file_path);
    if (current_item == nullptr)
    {
        shuffled_indices_.clear();
        current_shuffle_index_ = -1;
        current_playing_playlist_id_ = -1;
        return;
    }

    clicked_song_item_ = current_item;
    if (had_restored_item && restored_file_path == current_file_path)
    {
        restored_song_item_ = current_item;
    }
    mark_restored_song_item(current_item);

    if (current_mode_ == playback_mode::Shuffle && current_item->parent() != nullptr)
    {
        const int current_song_index = current_item->parent()->indexOfChild(current_item);
        generate_shuffled_list(current_item->parent(), current_song_index);
        current_shuffle_index_ = 0;
    }
}

void playlist_window::on_playlist_added(const Playlist& new_playlist)
{
    song_tree_widget_->blockSignals(true);
    auto* playlist_item = new QTreeWidgetItem(song_tree_widget_);
    set_item_text_with_tooltip(playlist_item, QString("%1 [0]").arg(new_playlist.name));
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
        const bool refreshes_playing_playlist = playlist_id == current_playing_playlist_id_ && !current_playing_file_path_.isEmpty();
        const bool refreshed_restored_item = restored_song_item_ != nullptr && restored_song_item_->parent() == item &&
                                             restored_song_item_->data(0, Qt::UserRole).toString() == current_playing_file_path_;
        const Playlist playlist = playlist_manager_->get_playlist_by_id(playlist_id);
        item->takeChildren();
        for (const auto& song : playlist.songs)
        {
            auto* song_item = new QTreeWidgetItem(item);
            song_item->setIcon(0, QIcon(":/icons/song.svg"));
            set_song_item_text_with_tooltip(song_item, song.file_name);
            song_item->setData(0, Qt::UserRole, song.file_path);
        }
        set_item_text_with_tooltip(item, QString("%1 [%2]").arg(playlist.name).arg(playlist.songs.count()));

        if (refreshes_playing_playlist)
        {
            QTreeWidgetItem* current_item = find_song_child_by_path(item, current_playing_file_path_);
            if (current_item != nullptr)
            {
                clicked_song_item_ = current_item;
                if (refreshed_restored_item)
                {
                    restored_song_item_ = current_item;
                }
                currently_playing_item_ = current_item;
                current_playing_playlist_id_ = item->data(0, Qt::UserRole).toLongLong();
                QFont font = currently_playing_item_->font(0);
                font.setBold(true);
                currently_playing_item_->setFont(0, font);
                currently_playing_item_->setForeground(0, QBrush(QColor("#3498DB")));
                currently_playing_item_->setBackground(0, QBrush(QColor("#A6E4FF")));
                if (current_mode_ == playback_mode::Shuffle)
                {
                    const int current_song_index = item->indexOfChild(current_item);
                    generate_shuffled_list(item, current_song_index);
                    current_shuffle_index_ = 0;
                }
            }
            else
            {
                currently_playing_item_ = nullptr;
                clicked_song_item_ = nullptr;
                restored_song_item_ = nullptr;
                shuffled_indices_.clear();
                current_shuffle_index_ = -1;
            }
        }
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
        const Playlist playlist = playlist_manager_->get_playlist_by_id(playlist_id);
        QSet<QString> known_paths;
        for (const auto& song : playlist.songs)
        {
            known_paths.insert(song.file_path);
        }

        QStringList files_to_add;
        int duplicate_count = 0;
        for (const QString& file : files)
        {
            if (known_paths.contains(file))
            {
                duplicate_count++;
                continue;
            }

            known_paths.insert(file);
            files_to_add.append(file);
        }

        if (files_to_add.isEmpty())
        {
            QMessageBox::information(this, "重复歌曲", "选择的歌曲已存在于当前播放列表。");
            return;
        }

        playlist_manager_->add_songs_to_playlist(playlist_id, files_to_add);

        if (duplicate_count > 0)
        {
            QMessageBox::information(this, "已跳过重复歌曲", QString("已添加 %1 首歌曲，跳过 %2 首已存在的歌曲。").arg(files_to_add.count()).arg(duplicate_count));
        }
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
    (void)column;
    play_song_item(item, true);
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

    QTreeWidgetItem* parent_playlist = currently_playing_item_->parent();
    if (parent_playlist == nullptr)
    {
        return;
    }

    QTreeWidgetItem* next_item = song_tree_widget_->itemBelow(currently_playing_item_);

    if (next_item == nullptr || next_item->parent() != parent_playlist)
    {
        if (current_mode_ == playback_mode::ListLoop)
        {
            next_item = parent_playlist->child(0);
        }
        else if (current_mode_ == playback_mode::Sequential)
        {
            on_stop_requested();
            return;
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

    QTreeWidgetItem* parent_playlist = currently_playing_item_->parent();
    if (parent_playlist == nullptr)
    {
        return;
    }

    QTreeWidgetItem* prev_item = song_tree_widget_->itemAbove(currently_playing_item_);

    if (prev_item == nullptr || prev_item->parent() != parent_playlist)
    {
        if (current_mode_ == playback_mode::ListLoop)
        {
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
        currently_playing_item_->setBackground(0, QBrush());
        currently_playing_item_ = nullptr;
    }
}

bool playlist_window::play_first_song_in_list()
{
    for (int i = 0; i < song_tree_widget_->topLevelItemCount(); ++i)
    {
        auto* playlist_item = song_tree_widget_->topLevelItem(i);
        if (playlist_item != nullptr && playlist_item->childCount() > 0)
        {
            on_tree_item_double_clicked(playlist_item->child(0), 0);
            return true;
        }
    }
    return false;
}
