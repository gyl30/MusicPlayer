#include "duration_scanner.h"
#include "log.h"
#include "scoped_exit.h"

extern "C"
{
#include <libavformat/avformat.h>
}

constexpr int kDurationBatchSize = 64;

duration_scanner::duration_scanner(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<QMap<QString, qint64>>("QMap<QString,qint64>");
    worker_ = std::thread(&duration_scanner::scan_loop, this);
}

duration_scanner::~duration_scanner() { shutdown(); }

void duration_scanner::shutdown()
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

void duration_scanner::enqueue(const QStringList& paths)
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

void duration_scanner::acknowledge(const QStringList& paths)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& path : paths)
    {
        scheduled_.remove(path);
    }
}

int duration_scanner::interrupt(void* context)
{
    return static_cast<duration_scanner*>(context)->stopped_.load() ? 1 : 0;
}

qint64 duration_scanner::probe_duration(const QString& path)
{
    AVFormatContext* context = avformat_alloc_context();
    if (context == nullptr)
    {
        return 0;
    }
    DEFER(avformat_close_input(&context));
    context->interrupt_callback = {interrupt, this};
    if (avformat_open_input(&context, path.toUtf8().constData(), nullptr, nullptr) < 0)
    {
        LOG_WARN("读取歌曲时长失败 {}", path.toStdString());
        return 0;
    }
    if (avformat_find_stream_info(context, nullptr) < 0)
    {
        LOG_WARN("读取歌曲流信息失败 {}", path.toStdString());
    }
    if (context->duration != AV_NOPTS_VALUE && context->duration > 0)
    {
        return context->duration / (AV_TIME_BASE / 1000);
    }
    const int index = av_find_best_stream(context, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (index >= 0)
    {
        const auto* stream = context->streams[index];
        if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0)
        {
            return av_rescale_q(stream->duration, stream->time_base, {1, 1000});
        }
    }
    return 0;
}

void duration_scanner::scan_loop()
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
            while (!pending_.empty() && batch.size() < kDurationBatchSize)
            {
                batch.append(pending_.front());
                pending_.pop_front();
            }
        }
        QMap<QString, qint64> durations;
        for (const auto& path : batch)
        {
            if (stopped_)
            {
                return;
            }
            durations.insert(path, probe_duration(path));
        }
        if (!stopped_)
        {
            emit durations_ready(durations);
        }
    }
}
