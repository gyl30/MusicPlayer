#ifndef AUDIO_PACKET
#define AUDIO_PACKET

#include <cstdint>
#include <vector>

struct audio_packet
{
    int64_t ms;
    std::vector<uint8_t> data;
};

#endif
