#ifndef LYRICS_PARSER_H
#define LYRICS_PARSER_H

#include <QString>
#include <QList>
#include "audio_packet.h"

class lyrics_parser
{
   public:
    static QList<LyricLine> parse(const QString& raw_lyrics);
};

#endif
