#ifndef SONG_DELEGATE_H
#define SONG_DELEGATE_H

#include <QStyledItemDelegate>

// 标记正在播放的歌曲节点，由 song_delegate 绘制高亮。
constexpr int kPlayingRole = Qt::UserRole + 1;

class song_delegate final : public QStyledItemDelegate
{
   public:
    // 歌单列表中歌曲是歌单的子节点；音乐库中每一行都是歌曲。
    enum class rows
    {
        nested_in_playlists,
        all_songs
    };

    explicit song_delegate(rows layout, QObject* parent = nullptr);

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

   private:
    rows layout_;
};

// 歌曲列表显示用的文本：还原常见 HTML 实体并去除首尾空白。
QString normalize_display_text(const QString& text);
// 未知时长显示为 --:--。
QString format_duration_ms(qint64 duration_ms);

#endif
