#ifndef MUSIC_LIBRARY_VIEW_H
#define MUSIC_LIBRARY_VIEW_H

#include <QSet>
#include <QTreeWidget>

class playlist_manager;
class song_delegate;

// 音乐库：列出所有导入过的音乐文件，与歌单无关地批量管理。
class music_library_view : public QTreeWidget
{
    Q_OBJECT

   public:
    explicit music_library_view(playlist_manager* manager, QWidget* parent = nullptr);

    void set_filter(const QString& keyword);
    // 选中并聚焦第一首未被过滤的歌曲，分组时会展开它所在的分组。
    void focus_first_match();

    enum class grouping
    {
        none,
        artist,
        album
    };
    void set_grouping(grouping mode);

   public slots:
    void add_selected_to_playlist(qint64 playlist_id);
    void on_delete_action();
    void on_cleanup_invalid_action();

   signals:
    void play_requested(const QString& file_path);

   protected:
    void showEvent(QShowEvent* event) override;

   private:
    void refresh();
    void refresh_when_visible();
    void show_context_menu(const QPoint& position);
    [[nodiscard]] QStringList selected_paths() const;

    playlist_manager* manager_ = nullptr;
    song_delegate* delegate_ = nullptr;
    grouping grouping_ = grouping::none;
    // 用户折叠的分组名称，刷新后保持折叠。
    QSet<QString> collapsed_groups_;
    QString filter_;
    // 隐藏时只记录数据已变化，显示时再重建，避免在歌单页操作时反复刷新。
    bool dirty_ = true;
};

#endif
