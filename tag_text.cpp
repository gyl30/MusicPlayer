#include <QByteArray>
#include <QStringDecoder>
#include <QTextCodec>

#include "tag_text.h"

// 严格按 GBK 解码。转换器会静默丢弃结尾不完整的字节，因此用重新编码是否与原字节一致来判断。
static bool decode_gbk(const QByteArray& data, QString& result)
{
    QTextCodec* codec = QTextCodec::codecForName("GBK");
    if (codec == nullptr)
    {
        return false;
    }
    QTextCodec::ConverterState state;
    result = codec->toUnicode(data.constData(), static_cast<int>(data.size()), &state);
    return state.invalidChars == 0 && codec->fromUnicode(result) == data;
}

QString decode_tag_text(const char* value)
{
    if (!value) return {};

    const QByteArray data(value);
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString text = utf8(data);
    QString gbk_text;
    if (utf8.hasError())
    {
        return decode_gbk(data, gbk_text) ? gbk_text : QString::fromLatin1(data);
    }

    // FFmpeg 把标为 ISO-8859-1 的 ID3 帧逐字节转成 UTF-8，老 MP3 常在其中存放 GBK 中文。
    // 文本全在 Latin-1 范围且含高位字符时，还原原始字节再尝试 GBK。
    bool has_high_byte = false;
    for (const QChar ch : text)
    {
        if (ch.unicode() > 0xFF) return text;
        has_high_byte = has_high_byte || ch.unicode() >= 0x80;
    }
    if (has_high_byte && decode_gbk(text.toLatin1(), gbk_text))
    {
        return gbk_text;
    }
    return text;
}

QString decode_text_file(const QByteArray& data)
{
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString text = utf8(data);
    if (!utf8.hasError())
    {
        return text;
    }
    QString gbk_text;
    return decode_gbk(data, gbk_text) ? gbk_text : QString::fromLatin1(data);
}
