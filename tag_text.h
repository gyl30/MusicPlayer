#ifndef TAG_TEXT_H
#define TAG_TEXT_H

#include <QString>

// 把 FFmpeg 读出的标签文本转为 QString，兼容老 MP3 中按 Latin-1 转换过的 GBK 中文。
QString decode_tag_text(const char* value);
// 文本文件内容：合法 UTF-8（可带 BOM）按 UTF-8，否则尝试 GBK，都不是时按 Latin-1。
QString decode_text_file(const QByteArray& data);

#endif
