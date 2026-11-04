#include "song_scanner.h"
#include "log.h"
#include "scoped_exit.h"
#include "tag_text.h"

extern "C"
{
#include <libavformat/avformat.h>
}

constexpr int kScanBatchSize = 64;

static std::string ffmpeg_error_string(int error_code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(error_code, buffer, sizeof(buffer));
    return buffer;
}

song_scanner::song_scanner(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<QMap<QString, scanned_song>>("QMap<QString,scanned_song>");
    worker_ = std::thread(&song_scanner::scan_loop, this);
}

song_scanner::~song_scanner() { shutdown(); }

void song_scanner::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }
    ready_.notify_all();
    if (worker_.joinable())
    {
        worker_.join();
    }
}

void song_scanner::enqueue(const QStringList& paths)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& path : paths)
        {
            if (scheduled_.contains(path))
            {
                continue;
            }
            scheduled_.insert(path);
            pending_.push_back(path);
        }
    }
    ready_.notify_one();
}

void song_scanner::acknowledge(const QStringList& paths)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& path : paths)
    {
        scheduled_.remove(path);
    }
}

int song_scanner::interrupt(void* context)
{
    return static_cast<song_scanner*>(context)->stopped_.load() ? 1 : 0;
}

// 容器标签优先；Ogg/Opus 等格式的标签保存在音频流上。
static QString read_tag(const AVFormatContext* context, const AVStream* stream, const char* key)
{
    const AVDictionaryEntry* entry = av_dict_get(context->metadata, key, nullptr, 0);
    if (entry == nullptr && stream != nullptr)
    {
        entry = av_dict_get(stream->metadata, key, nullptr, 0);
    }
    return entry != nullptr ? decode_tag_text(entry->value).trimmed() : QString();
}

scanned_song song_scanner::scan(const QString& path)
{
    scanned_song result;
    AVFormatContext* context = avformat_alloc_context();
    if (context == nullptr)
    {
        return result;
    }
    DEFER(avformat_close_input(&context));
    context->interrupt_callback = {interrupt, this};
    int ret = avformat_open_input(&context, path.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0)
    {
        LOG_WARN("读取歌曲信息失败 file {} error {}", path.toStdString(), ffmpeg_error_string(ret));
        return result;
    }
    ret = avformat_find_stream_info(context, nullptr);
    if (ret < 0)
    {
        LOG_WARN("读取歌曲流信息失败 file {} error {}", path.toStdString(), ffmpeg_error_string(ret));
    }
    const int index = av_find_best_stream(context, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    const AVStream* stream = index >= 0 ? context->streams[index] : nullptr;
    result.title = read_tag(context, stream, "title");
    result.artist = read_tag(context, stream, "artist");
    result.album = read_tag(context, stream, "album");
    result.duration_ms = 0;
    if (context->duration != AV_NOPTS_VALUE && context->duration > 0)
    {
        result.duration_ms = context->duration / (AV_TIME_BASE / 1000);
    }
    else if (stream != nullptr && stream->duration != AV_NOPTS_VALUE && stream->duration > 0)
    {
        result.duration_ms = av_rescale_q(stream->duration, stream->time_base, {1, 1000});
    }
    return result;
}

void song_scanner::scan_loop()
{
    while (!stopped_)
    {
        QStringList batch;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock, [this] { return stopped_ || !pending_.empty(); });
            if (stopped_)
            {
                return;
            }
            while (!pending_.empty() && batch.size() < kScanBatchSize)
            {
                batch.append(pending_.front());
                pending_.pop_front();
            }
        }
        QMap<QString, scanned_song> results;
        for (const auto& path : batch)
        {
            if (stopped_)
            {
                return;
            }
            results.insert(path, scan(path));
        }
        if (!stopped_)
        {
            emit songs_scanned(results);
        }
    }
}
