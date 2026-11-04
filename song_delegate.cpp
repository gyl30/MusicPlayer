#include <QApplication>
#include <QPainter>
#include <QWidget>

#include "song_delegate.h"

song_delegate::song_delegate(rows layout, QObject* parent) : QStyledItemDelegate(parent), layout_(layout) {}

QSize song_delegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& /*index*/) const
{
    return QSize(80, option.widget != nullptr && option.widget->width() >= 460 ? 40 : 34);
}

void song_delegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();
    const bool song = layout_ == rows::all_songs || index.parent().isValid();
    const bool playing = song && index.siblingAtColumn(0).data(kPlayingRole).toBool();
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const bool dark = qApp->property("darkMode").toBool();
    if (playing || selected || hovered)
        painter->fillRect(option.rect, playing ? QColor(dark ? "#304D6C" : "#E4F0FF")
                                                  : selected ? QColor(dark ? "#30445F" : "#E1E7F0")
                                                             : QColor(dark ? "#29394D" : "#EEF3FA"));
    QFont font = option.font;
    font.setPointSize(option.widget != nullptr && option.widget->width() >= 460 ? 11 : 10);
    font.setBold(playing || !song);
    painter->setFont(font);
    painter->setPen(playing ? QColor(dark ? "#74B6FF" : "#1478E8")
                            : song && index.column() == 0 ? QColor(dark ? "#E6EDF7" : "#26364E")
                                                          : QColor(dark ? "#A2B2C7" : "#64758D"));
    QRect text = option.rect.adjusted(4, 0, -12, 0);
    painter->drawText(text, Qt::AlignVCenter | (index.column() == 1 ? Qt::AlignRight : Qt::AlignLeft),
                      painter->fontMetrics().elidedText(index.data().toString(), Qt::ElideRight, text.width()));
    if (song)
    {
        painter->setPen(QColor(dark ? "#3B4C61" : "#E8EDF5"));
        painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
    }
    if (option.state & QStyle::State_HasFocus)
    {
        painter->setPen(QPen(QColor(dark ? "#83ACD8" : "#7899C2"), 1, Qt::DotLine));
        painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
    }
    painter->restore();
}

QString normalize_display_text(const QString& text)
{
    QString display_text = text.trimmed();
    display_text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    display_text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    display_text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    display_text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    return display_text;
}

QString format_duration_ms(qint64 duration_ms)
{
    if (duration_ms <= 0)
    {
        return QStringLiteral("--:--");
    }

    const qint64 total_seconds = duration_ms / 1000;
    const qint64 hours = total_seconds / 3600;
    const qint64 minutes = (total_seconds % 3600) / 60;
    const qint64 seconds = total_seconds % 60;
    if (hours > 0)
    {
        return QString("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'));
    }

    return QString("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}
