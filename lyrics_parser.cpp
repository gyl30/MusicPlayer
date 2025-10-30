#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include "log.h"
#include "lyrics_parser.h"

QList<LyricLine> lyrics_parser::parse(const QString& raw_lyrics)
{
    QList<LyricLine> parsed_lyrics;
    if (raw_lyrics.isEmpty())
    {
        return parsed_lyrics;
    }

    QRegularExpression regex(R"(^\s*\[(\d{2}):(\d{2})(?:[\.:](\d{2,3}))?\]\s*(.*))");
    const QStringList lines = raw_lyrics.split('\n');

    for (const QString& line : lines)
    {
        QRegularExpressionMatch match = regex.match(line);
        if (match.hasMatch())
        {
            qint64 minutes = match.captured(1).toLongLong();
            qint64 seconds = match.captured(2).toLongLong();
            qint64 milliseconds = 0;

            if (match.hasCaptured(3))
            {
                milliseconds = match.captured(3).toLongLong();
                if (match.captured(3).length() == 2)
                {
                    milliseconds *= 10;
                }
            }

            QString text = match.captured(4).trimmed();
            qint64 total_ms = (minutes * 60 * 1000) + (seconds * 1000) + milliseconds;
            parsed_lyrics.append({total_ms, text});
        }
    }

    if (parsed_lyrics.isEmpty() && !raw_lyrics.trimmed().isEmpty())
    {
        LOG_INFO("LRC parsing yielded no timed lines. Treating content as plain text lyrics.");
        parsed_lyrics.append({0, raw_lyrics.trimmed()});
    }

    return parsed_lyrics;
}
