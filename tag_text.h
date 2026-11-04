#ifndef TAG_TEXT_H
#define TAG_TEXT_H

#include <QString>

// 把 FFmpeg 读出的标签文本转为 QString，兼容老 MP3 中按 Latin-1 转换过的 GBK 中文。
QString decode_tag_text(const char* value);

#endif
