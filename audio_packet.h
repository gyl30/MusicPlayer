#ifndef AUDIO_PACKET
#define AUDIO_PACKET

#include <cstdint>
#include <vector>
#include <QMetaType>
#include <QString>
#include <QList>

struct audio_packet
{
    int64_t ms;
    std::vector<uint8_t> data;
};

struct LyricLine
{
    qint64 timestamp_ms;
    QString text;
};

Q_DECLARE_METATYPE(std::shared_ptr<audio_packet>);
Q_DECLARE_METATYPE(QList<LyricLine>);

#endif
