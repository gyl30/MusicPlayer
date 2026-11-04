#include <QByteArray>
#include <QString>
#include <QtGlobal>
#include <cstdarg>
#include <string>
#include <vector>
#include <memory>
#include "log.h"
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

extern "C"
{
#include <libavutil/log.h>
}

static void init_default_log(const spdlog::filename_t& filename);
static void set_log_level();
static uint32_t get_log_file_size();
static uint32_t get_log_file_count();
static void forward_qt_message(QtMsgType type, const QMessageLogContext& context, const QString& message);
static void forward_ffmpeg_message(void* avcl, int level, const char* format, va_list args);

void init_log(const spdlog::filename_t& filename)
{
    init_default_log(filename);

    set_log_level();
    // Qt 与 FFmpeg 默认只写 stderr，图形界面启动时会丢失，统一写入日志文件。
    qInstallMessageHandler(forward_qt_message);
    av_log_set_callback(forward_ffmpeg_message);
}
void set_level(const std::string& level)
{
    spdlog::set_level(spdlog::level::info);
    if (level == "debug")
    {
        spdlog::set_level(spdlog::level::debug);
    }
    else if (level == "warn" || level == "warning")
    {
        spdlog::set_level(spdlog::level::warn);
    }
    else if (level == "err" || level == "error")
    {
        spdlog::set_level(spdlog::level::err);
    }
    else if (level == "trace")
    {
        spdlog::set_level(spdlog::level::trace);
    }
}
void shutdown_log()
{
    spdlog::default_logger()->flush();
    spdlog::shutdown();
}

static void init_default_log(const spdlog::filename_t& filename)
{
    uint32_t file_size = get_log_file_size();
    uint32_t file_count = get_log_file_count();
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(filename, file_size, file_count));
    auto logger = std::make_shared<spdlog::logger>("", begin(sinks), end(sinks));
    spdlog::set_default_logger(logger);
    spdlog::flush_every(std::chrono::seconds(3));
    // 警告和错误立即落盘，避免崩溃或强制退出时丢失最关键的几行。
    spdlog::flush_on(spdlog::level::warn);
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("%Y%m%d %T.%f %t %L %v %s %#");
}

static void set_log_level()
{
    spdlog::set_level(spdlog::level::info);
    if (qEnvironmentVariableIsSet("TRACE"))
    {
        spdlog::set_level(spdlog::level::trace);
    }
    else if (qEnvironmentVariableIsSet("DEBUG"))
    {
        spdlog::set_level(spdlog::level::debug);
    }
}
static uint32_t get_log_file_size()
{
    constexpr auto kFileSize = 50 * 1024 * 1024;
    bool ok = false;
    const uint32_t file_size = qgetenv("LOG_FILE_SIZE").toUInt(&ok);
    if (ok && file_size > 0)
    {
        return file_size;
    }
    return kFileSize;
}
static uint32_t get_log_file_count()
{
    constexpr auto kFileCount = 5;
    bool ok = false;
    const uint32_t file_count = qgetenv("LOG_FILE_COUNT").toUInt(&ok);
    if (ok && file_count > 0)
    {
        return file_count;
    }
    return kFileCount;
}

static void forward_qt_message(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    spdlog::level::level_enum level = spdlog::level::err;
    switch (type)
    {
        case QtDebugMsg:
            level = spdlog::level::debug;
            break;
        case QtInfoMsg:
            level = spdlog::level::info;
            break;
        case QtWarningMsg:
            level = spdlog::level::warn;
            break;
        case QtCriticalMsg:
        case QtFatalMsg:
            level = spdlog::level::err;
            break;
    }
    const spdlog::source_loc location{context.file != nullptr ? context.file : "", context.line, context.function != nullptr ? context.function : ""};
    spdlog::default_logger_raw()->log(location, level, "qt {}", message.toStdString());
}

static void forward_ffmpeg_message(void* avcl, int level, const char* format, va_list args)
{
    // 解码中的 error 多为可恢复的坏帧，记为警告；warning 量大（如按码率估算时长），仅调试级别可见。
    if (level > AV_LOG_WARNING)
    {
        return;
    }
    const auto spd_level = level <= AV_LOG_ERROR ? spdlog::level::warn : spdlog::level::debug;
    if (!spdlog::should_log(spd_level))
    {
        return;
    }
    // 不用 FFmpeg 自带的 "[mp3 @ 0x...]" 前缀，只记录组件名。
    const AVClass* av_class = avcl != nullptr ? *static_cast<AVClass**>(avcl) : nullptr;
    const char* component = av_class != nullptr ? av_class->item_name(avcl) : "";
    char line[1024];
    int print_prefix = 0;
    av_log_format_line2(avcl, level, format, args, line, sizeof(line), &print_prefix);
    std::string text(line);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
    {
        text.pop_back();
    }
    if (text.empty())
    {
        return;
    }
    if (*component == '\0')
    {
        spdlog::log(spd_level, "ffmpeg {}", text);
        return;
    }
    spdlog::log(spd_level, "ffmpeg {} {}", component, text);
}
