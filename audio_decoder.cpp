#include <QByteArray>
#include <QMap>
#include <QMetaObject>
#include <algorithm>
#include <QRegularExpression>
#include "log.h"
#include "scoped_exit.h"
#include "audio_decoder.h"

static std::string ffmpeg_error_string(int error_code)
{
    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(error_code, errbuf, AV_ERROR_MAX_STRING_SIZE);
    return errbuf;
}

static AVSampleFormat get_av_sample_format(QAudioFormat::SampleFormat format)
{
    switch (format)
    {
        case QAudioFormat::Int16:
            return AV_SAMPLE_FMT_S16;
        case QAudioFormat::Int32:
            return AV_SAMPLE_FMT_S32;
        case QAudioFormat::Float:
            return AV_SAMPLE_FMT_FLT;
        default:
            return AV_SAMPLE_FMT_S16;
    }
}

audio_decoder::audio_decoder(QObject* parent) : QObject(parent) { decoding_paused_.store(true); }

audio_decoder::~audio_decoder() { close_audio_context(); }

void audio_decoder::shutdown() { stop_flag_ = true; }

void audio_decoder::seek(qint64 session_id, qint64 position_ms)
{
    LOG_INFO("跳转流程 3/10 解码器收到跳转请求 目标 {}ms 会话ID {}", position_ms, session_id);
    seek_session_id_ = session_id;
    seek_position_ms_ = position_ms;
    seek_requested_ = true;
    QMetaObject::invokeMethod(this, "do_seek", Qt::QueuedConnection);
}

void audio_decoder::start_decoding(qint64 session_id, const QString& file, qint64 offset)
{
    session_id_ = session_id;
    LOG_INFO("播放流程 3/14 解码器收到启动请求 会话ID {}", session_id_);

    if (!stop_flag_.load())
    {
        LOG_WARN("解码器已在运行 将先停止之前的任务");
        stop_flag_ = true;
    }
    LOG_INFO("播放流程 4/14 重置解码器内部状态 会话ID {}", session_id_);
    file_path_ = file;
    seek_requested_ = false;
    seek_position_ms_ = -1;
    first_frame_processed_ = false;
    start_time_offset_ms_ = 0;
    decoding_paused_ = true;

    LOG_INFO("播放流程 5/14 正在打开音频文件 会话ID {}", session_id_);
    if (!open_audio_context(file_path_))
    {
        LOG_ERROR("初始化音频上下文失败 {}", file.toStdString());
        emit decoding_error("Failed to open audio file");
        return;
    }

    accumulated_ms_ = 0;
    stop_flag_ = false;

    if (offset > 0)
    {
        seek(session_id_, offset);
    }
    else
    {
        first_frame_processed_ = false;
    }

    LOG_INFO("会话 {} 解码器探测文件成功 正在等待恢复信号", session_id_);
}

void audio_decoder::resume_decoding()
{
    if (stop_flag_)
    {
        LOG_WARN("请求恢复解码 但解码器已停止");
        return;
    }

    if (!decoding_paused_.exchange(false))
    {
        return;
    }

    LOG_TRACE("为会话 {} 恢复/启动解码循环", session_id_);
    QMetaObject::invokeMethod(this, "do_decoding_cycle", Qt::QueuedConnection);
}

void audio_decoder::pause_decoding()
{
    decoding_paused_ = true;
    LOG_DEBUG("解码器已收到暂停请求");
}

void audio_decoder::do_decoding_cycle()
{
    if (stop_flag_ || decoding_paused_)
    {
        if (decoding_paused_)
        {
            LOG_DEBUG("解码循环已暂停");
        }
        if (stop_flag_)
        {
            LOG_DEBUG("解码已按请求停止");
            close_audio_context();
            emit decoding_finished();
        }
        return;
    }

    int receive_ret = avcodec_receive_frame(codec_ctx_, frame_);

    if (receive_ret == 0)
    {
        process_frame(frame_);
        QMetaObject::invokeMethod(this, "do_decoding_cycle", Qt::QueuedConnection);
        return;
    }

    if (receive_ret == AVERROR_EOF)
    {
        LOG_INFO("结束流程 1/4 文件解码完成 发送文件结束信号 会话ID {}", session_id_);
        emit packet_ready(session_id_, nullptr);
        LOG_INFO("会话 {} 已达文件末尾 解码器现在空闲", session_id_);
        return;
    }

    if (receive_ret != AVERROR(EAGAIN))
    {
        LOG_ERROR("接收帧失败 {}", ffmpeg_error_string(receive_ret));
        return;
    }

    int read_ret = av_read_frame(format_ctx_, packet_);
    if (read_ret < 0)
    {
        avcodec_send_packet(codec_ctx_, nullptr);
        QMetaObject::invokeMethod(this, "do_decoding_cycle", Qt::QueuedConnection);
        return;
    }

    DEFER(av_packet_unref(packet_));
    if (packet_->stream_index != audio_stream_index_)
    {
        QMetaObject::invokeMethod(this, "do_decoding_cycle", Qt::QueuedConnection);
        return;
    }

    int send_ret = avcodec_send_packet(codec_ctx_, packet_);
    if (send_ret < 0)
    {
        LOG_WARN("发送数据包失败 {}", ffmpeg_error_string(send_ret));
    }

    QMetaObject::invokeMethod(this, "do_decoding_cycle", Qt::QueuedConnection);
}

void audio_decoder::do_seek()
{
    LOG_INFO("跳转流程 4/10 解码器正在执行物理跳转 会话ID {}", seek_session_id_);
    if (!seek_requested_)
    {
        return;
    }

    auto guard = make_scoped_exit(
        [this]()
        {
            seek_requested_ = false;
            LOG_INFO("跳转流程 5/10 跳转失败 通知控制中心 会话ID {}", seek_session_id_);
            emit seek_finished(seek_session_id_, -1);
        });

    if (format_ctx_ == nullptr)
    {
        LOG_WARN("会话 {} 跳转被忽略 因为音频上下文未打开", seek_session_id_);
        return;
    }

    qint64 current_seek_id = seek_session_id_;
    qint64 target_pos_ms = seek_position_ms_;

    qint64 seek_target_ts = av_rescale_q(target_pos_ms, {1, 1000}, time_base_);

    LOG_DEBUG("跳转到 目标相对毫秒 {} 目标时间戳 {}", target_pos_ms, seek_target_ts);
    int ret = av_seek_frame(format_ctx_, audio_stream_index_, seek_target_ts, AVSEEK_FLAG_BACKWARD);

    if (ret < 0)
    {
        LOG_WARN("会话 {} av_seek_frame 跳转至 {}ms 失败 {}", current_seek_id, target_pos_ms, ffmpeg_error_string(ret));
        return;
    }

    avcodec_flush_buffers(codec_ctx_);
    LOG_DEBUG("跳转后解码器缓冲区已清空");

    while (true)
    {
        int receive_ret = avcodec_receive_frame(codec_ctx_, frame_);
        if (receive_ret == 0)
        {
            qint64 actual_ms = 0;
            if (frame_->pts != AV_NOPTS_VALUE)
            {
                auto raw_actual_ms = static_cast<qint64>(av_q2d(time_base_) * 1000 * static_cast<double>(frame_->pts));
                start_time_offset_ms_ = raw_actual_ms - target_pos_ms;
                actual_ms = target_pos_ms;
                LOG_INFO("跳转后首帧PTS {} 重新校准时间偏移为 {}", frame_->pts, start_time_offset_ms_);
            }
            else
            {
                actual_ms = target_pos_ms;
            }

            accumulated_ms_ = actual_ms;

            first_frame_processed_ = true;

            LOG_INFO("会话 {} 跳转成功, 第一帧 pts {} 对应精确时间 {}ms", current_seek_id, frame_->pts, actual_ms);
            LOG_INFO("跳转流程 5/10 跳转成功 通知控制中心 会话ID {}", current_seek_id);
            emit seek_finished(current_seek_id, actual_ms);

            process_frame(frame_);

            guard.cancel();
            seek_requested_ = false;
            return;
        }

        if (receive_ret != AVERROR(EAGAIN))
        {
            LOG_ERROR("跳转后接收帧失败 {}", ffmpeg_error_string(receive_ret));
            return;
        }

        int read_ret = av_read_frame(format_ctx_, packet_);
        if (read_ret < 0)
        {
            LOG_WARN("跳转后 av_read_frame 失败 可能已达文件末尾 {}", ffmpeg_error_string(read_ret));
            return;
        }

        if (packet_->stream_index == audio_stream_index_)
        {
            int send_ret = avcodec_send_packet(codec_ctx_, packet_);
            av_packet_unref(packet_);
            if (send_ret < 0)
            {
                LOG_WARN("跳转后发送数据包失败 {}", ffmpeg_error_string(send_ret));
                return;
            }
        }
        else
        {
            av_packet_unref(packet_);
        }
    }
}

void audio_decoder::process_frame(AVFrame* frame)
{
    if (frame->pts != AV_NOPTS_VALUE && !first_frame_processed_)
    {
        start_time_offset_ms_ = static_cast<qint64>(av_q2d(time_base_) * 1000 * static_cast<double>(frame->pts));
        first_frame_processed_ = true;
        LOG_INFO("首帧PTS {} 动态时间戳偏移校准为 {} ms", frame->pts, start_time_offset_ms_);
    }

    int converted_samples = swr_convert(swr_ctx_, &swr_data_, target_format_.sampleRate(), (const uint8_t**)frame->data, frame->nb_samples);
    if (converted_samples < 0)
    {
        LOG_ERROR("转换失败 {}", ffmpeg_error_string(converted_samples));
        return;
    }

    int buffer_size = av_samples_get_buffer_size(&swr_data_linesize_, target_format_.channelCount(), converted_samples, target_ffmpeg_fmt_, 1);

    qint64 timestamp_ms = 0;
    if (frame->pts != AV_NOPTS_VALUE)
    {
        auto raw_timestamp_ms = static_cast<qint64>(av_q2d(time_base_) * 1000 * static_cast<double>(frame->pts));
        timestamp_ms = raw_timestamp_ms - start_time_offset_ms_;

        LOG_DEBUG("解码帧 PTS {} time_base {}/{} raw_ms {} offset_ms {} final_ms {}",
                  frame->pts,
                  time_base_.num,
                  time_base_.den,
                  raw_timestamp_ms,
                  start_time_offset_ms_,
                  timestamp_ms);
    }
    else
    {
        qint64 duration_ms = static_cast<qint64>(frame->nb_samples) * 1000 / codec_ctx_->sample_rate;
        timestamp_ms = accumulated_ms_ + duration_ms;

        LOG_WARN("解码帧 无PTS估算下一时间戳 accumulated_ms {} duration_ms {} final_ms {}", accumulated_ms_, duration_ms, timestamp_ms);
    }

    accumulated_ms_ = timestamp_ms;
    timestamp_ms = std::max<qint64>(timestamp_ms, 0);

    LOG_TRACE("会话 {} 解码数据包 长度 {} pts {} 时间戳 {}ms", session_id_, buffer_size, frame->pts, timestamp_ms);
    if (buffer_size > 0)
    {
        auto packet = std::make_shared<audio_packet>();
        packet->data.assign(swr_data_, swr_data_ + buffer_size);
        packet->ms = timestamp_ms;
        emit packet_ready(session_id_, packet);
    }
}

static QList<LyricLine> parse_lrc(const QString& lrc_text)
{
    QList<LyricLine> lyrics;
    if (lrc_text.isEmpty())
    {
        return lyrics;
    }

    QRegularExpression regex(R"(\[(\d{2}):(\d{2})[\.:](\d{2,3})\](.*))");
    const QStringList lines = lrc_text.split('\n');

    for (const QString& line : lines)
    {
        QRegularExpressionMatch match = regex.match(line);
        if (match.hasMatch())
        {
            qint64 minutes = match.captured(1).toLongLong();
            qint64 seconds = match.captured(2).toLongLong();
            qint64 milliseconds = match.captured(3).toLongLong();
            if (match.captured(3).length() == 2)
            {
                milliseconds *= 10;
            }
            QString text = match.captured(4).trimmed();

            qint64 total_ms = (minutes * 60 * 1000) + (seconds * 1000) + milliseconds;
            lyrics.append({total_ms, text});
        }
    }
    return lyrics;
}

void audio_decoder::process_metadata(AVDictionary* metadata)
{
    QMap<QString, QString> metadata_map;
    AVDictionaryEntry* tag = nullptr;
    bool lyrics_found = false;
    while ((tag = av_dict_get(metadata, "", tag, AV_DICT_IGNORE_SUFFIX)) != nullptr)
    {
        QString key = QString::fromUtf8(tag->key);
        QString value = QString::fromUtf8(tag->value);
        metadata_map.insert(key, value);

        if (key.compare("comment", Qt::CaseInsensitive) == 0 || key.compare("lyrics", Qt::CaseInsensitive) == 0)
        {
            QList<LyricLine> parsed_lyrics = parse_lrc(value);
            if (!parsed_lyrics.isEmpty())
            {
                LOG_INFO("会话 {} 找到歌词元数据并解析成功", session_id_);
                emit lyrics_ready(session_id_, parsed_lyrics);
                lyrics_found = true;
            }
        }

        LOG_TRACE("元数据 {} {}", key.toStdString(), value.toStdString());
    }

    if (!lyrics_found)
    {
        emit lyrics_ready(session_id_, {});
    }

    if (!metadata_map.isEmpty())
    {
        emit metadata_ready(session_id_, metadata_map);
    }
}
bool audio_decoder::open_audio_context(const QString& file_path)
{
    auto guard = make_scoped_exit([this]() { close_audio_context(); });

    format_ctx_ = avformat_alloc_context();
    int ret = avformat_open_input(&format_ctx_, file_path.toUtf8().constData(), nullptr, nullptr);
    if (ret != 0)
    {
        LOG_ERROR("打开输入失败 {} {}", file_path.toStdString(), ffmpeg_error_string(ret));
        return false;
    }
    LOG_INFO("打开输入文件 {}", file_path.toStdString());

    ret = avformat_find_stream_info(format_ctx_, nullptr);
    if (ret < 0)
    {
        LOG_ERROR("查找流信息失败 {}", ffmpeg_error_string(ret));
        return false;
    }

    for (unsigned int i = 0; i < format_ctx_->nb_streams; i++)
    {
        AVStream* st = format_ctx_->streams[i];
        if ((st->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0)
        {
            if ((st->attached_pic.data != nullptr) && st->attached_pic.size > 0)
            {
                QByteArray image_data(reinterpret_cast<const char*>(st->attached_pic.data), st->attached_pic.size);
                LOG_INFO("会话 {} 找到封面 大小 {} 字节", session_id_, image_data.size());
                emit cover_art_ready(session_id_, image_data);
                break;
            }
        }
    }

    audio_stream_index_ = av_find_best_stream(format_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_stream_index_ < 0)
    {
        LOG_ERROR("无法在文件中找到音频流 {}", file_path.toStdString());
        return false;
    }
    LOG_INFO("找到音频流索引 {}", audio_stream_index_);

    AVStream* audio_stream = format_ctx_->streams[audio_stream_index_];
    time_base_ = audio_stream->time_base;

    av_dump_format(format_ctx_, audio_stream_index_, nullptr, 0);

    const AVCodec* codec = avcodec_find_decoder(audio_stream->codecpar->codec_id);
    if (codec == nullptr)
    {
        LOG_ERROR("未找到编解码器 ID {}", (int)audio_stream->codecpar->codec_id);
        return false;
    }

    if (format_ctx_->metadata != nullptr)
    {
        process_metadata(format_ctx_->metadata);
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (codec_ctx_ == nullptr)
    {
        LOG_ERROR("无法分配编解码器上下文");
        return false;
    }

    ret = avcodec_parameters_to_context(codec_ctx_, audio_stream->codecpar);
    if (ret < 0)
    {
        LOG_ERROR("编解码器参数到上下文失败 {}", ffmpeg_error_string(ret));
        return false;
    }
    LOG_INFO("编解码器参数到上下文成功 {}", codec->name);

    if (avcodec_open2(codec_ctx_, codec, nullptr) < 0)
    {
        LOG_ERROR("打开编解码器失败 {} {}", codec->name, ffmpeg_error_string(ret));
        return false;
    }
    LOG_INFO("打开编解码器成功 {}", codec->name);

    target_format_.setSampleRate(codec_ctx_->sample_rate);
    target_format_.setChannelCount(codec_ctx_->ch_layout.nb_channels);
    target_format_.setSampleFormat(QAudioFormat::Int16);
    target_ffmpeg_fmt_ = get_av_sample_format(target_format_.sampleFormat());

    int target_sample_rate = target_format_.sampleRate();
    AVChannelLayout target_ch_layout;
    av_channel_layout_default(&target_ch_layout, target_format_.channelCount());

    LOG_INFO("配置重采样器 源 速率 {} 格式 {} 布局 {} -> 目标 速率 {} 格式 {} 布局 {}",
             codec_ctx_->sample_rate,
             av_get_sample_fmt_name(codec_ctx_->sample_fmt),
             codec_ctx_->ch_layout.nb_channels,
             target_sample_rate,
             av_get_sample_fmt_name(target_ffmpeg_fmt_),
             target_format_.channelCount());

    ret = swr_alloc_set_opts2(&swr_ctx_,
                              &target_ch_layout,
                              target_ffmpeg_fmt_,
                              target_sample_rate,
                              &codec_ctx_->ch_layout,
                              codec_ctx_->sample_fmt,
                              codec_ctx_->sample_rate,
                              0,
                              nullptr);
    if (ret != 0)
    {
        LOG_ERROR("swr_alloc_set_opts 失败 {}", ffmpeg_error_string(ret));
        return false;
    }

    ret = swr_init(swr_ctx_);
    if (ret != 0)
    {
        LOG_ERROR("swr_init 失败 {}", ffmpeg_error_string(ret));
        return false;
    }
    AVSampleFormat target_fmt = get_av_sample_format(target_format_.sampleFormat());

    int alloc_ret = av_samples_alloc(&swr_data_, &swr_data_linesize_, target_format_.channelCount(), target_format_.sampleRate(), target_fmt, 0);
    if (alloc_ret < 0)
    {
        LOG_ERROR("样本分配失败 {}", ffmpeg_error_string(alloc_ret));
        return false;
    }

    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (frame_ == nullptr || packet_ == nullptr)
    {
        LOG_ERROR("帧或包分配失败");
        return false;
    }

    if (format_ctx_->duration != AV_NOPTS_VALUE)
    {
        qint64 duration_ms = format_ctx_->duration / (AV_TIME_BASE / 1000);
        emit duration_ready(session_id_, duration_ms, target_format_);
        LOG_INFO("播放流程 6/14 解码器准备就绪 发送音频信息至控制中心 时长 {}ms 会话ID {}", duration_ms, session_id_);
    }

    guard.cancel();
    return true;
}

void audio_decoder::close_audio_context()
{
    if (codec_ctx_ != nullptr)
    {
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
    }
    if (format_ctx_ != nullptr)
    {
        avformat_close_input(&format_ctx_);
        format_ctx_ = nullptr;
    }
    if (swr_ctx_ != nullptr)
    {
        swr_free(&swr_ctx_);
        swr_ctx_ = nullptr;
    }
    if (frame_ != nullptr)
    {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }
    if (packet_ != nullptr)
    {
        av_packet_free(&packet_);
        packet_ = nullptr;
    }
    if (swr_data_ != nullptr)
    {
        av_freep(&swr_data_);
    }
}
