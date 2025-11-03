#include <QRegularExpression>
#include <QRegularExpressionMatchIterator>
#include <algorithm>
#include "log.h"
#include "lyrics_parser.h"

QList<LyricLine> lyrics_parser::parse(const QString& raw_lyrics)
{
    QList<LyricLine> parsed_lyrics;
    if (raw_lyrics.isEmpty())
    {
        return parsed_lyrics;
    }

    QRegularExpression time_tag_regex(R"(\[(\d{2}):(\d{2})(?:[.:](\d{2,3}))?\])");
    const QStringList lines = raw_lyrics.split('\n');

    for (const QString& line : lines)
    {
        QRegularExpressionMatchIterator it = time_tag_regex.globalMatch(line);
        QList<qint64> timestamps_ms;
        qsizetype last_match_end_pos = 0;

        while (it.hasNext())
        {
            QRegularExpressionMatch match = it.next();
            qint64 minutes = match.captured(1).toLongLong();
            qint64 seconds = match.captured(2).toLongLong();
            qint64 milliseconds = 0;

            if (match.capturedLength(3) > 0)
            {
                milliseconds = match.captured(3).toLongLong();
                if (match.capturedLength(3) == 2)
                {
                    milliseconds *= 10;
                }
            }

            qint64 total_ms = (minutes * 60 * 1000) + (seconds * 1000) + milliseconds;
            timestamps_ms.append(total_ms);
            last_match_end_pos = match.capturedEnd();
        }

        if (!timestamps_ms.isEmpty())
        {
            QString text = line.mid(last_match_end_pos).trimmed();
            for (qint64 ts : timestamps_ms)
            {
                parsed_lyrics.append({ts, text});
            }
        }
    }

    std::sort(parsed_lyrics.begin(), parsed_lyrics.end(), [](const LyricLine& a, const LyricLine& b) { return a.timestamp_ms < b.timestamp_ms; });

    if (parsed_lyrics.isEmpty() && !raw_lyrics.trimmed().isEmpty())
    {
        LOG_INFO("LRC解析未产生带时间的歌词");
    }

    return parsed_lyrics;
}
