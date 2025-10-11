#ifndef AUDIO_PACKET
#define AUDIO_PACKET

#include <cstdint>
#include <vector>
#include <QMetaType>

struct audio_packet
{
    int64_t ms;
    std::vector<uint8_t> data;
};
Q_DECLARE_METATYPE(std::shared_ptr<audio_packet>);

#endif
