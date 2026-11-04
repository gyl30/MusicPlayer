#ifndef MUSIC_LIBRARY_VIEW_H
#define MUSIC_LIBRARY_VIEW_H

#include <QTreeWidget>

class playlist_manager;

// 音乐库：列出所有导入过的音乐文件，与歌单无关地批量管理。
class music_library_view : public QTreeWidget
{
    Q_OBJECT

   public:
    explicit music_library_view(playlist_manager* manager, QWidget* parent = nullptr);

    void set_filter(const QString& keyword);

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
    QString filter_;
    // 隐藏时只记录数据已变化，显示时再重建，避免在歌单页操作时反复刷新。
    bool dirty_ = true;
};

#endif
