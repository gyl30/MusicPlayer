#include <QMetaObject>
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

audio_decoder::audio_decoder(QObject* parent) : QObject(parent) {}

audio_decoder::~audio_decoder() { close_audio_context(); }

void audio_decoder::shutdown() { stop_flag_ = true; }

void audio_decoder::seek(qint64 session_id, qint64 position_ms)
{
    LOG_INFO("flow seek 3/10 received seek request for session {} to {}ms", session_id, position_ms);
    seek_session_id_ = session_id;
    seek_position_ms_ = position_ms;
    seek_requested_ = true;
    QMetaObject::invokeMethod(this, "do_seek", Qt::QueuedConnection);
}

void audio_decoder::start_decoding(qint64 session_id, const QString& file, const QAudioFormat& fmt, qint64 offset)
{
    session_id_ = session_id;
    LOG_INFO("flow 3/14 received start request for session {}", session_id_);

    if (!stop_flag_.load())
    {
        LOG_WARN("decoder is already running stopping previous task first");
        stop_flag_ = true;
    }
    LOG_INFO("flow 4/14 resetting internal state for session {}", session_id_);
    file_path_ = file;
    target_format_ = fmt;
    target_ffmpeg_fmt_ = get_av_sample_format(target_format_.sampleFormat());

    seek_requested_ = false;
    seek_position_ms_ = -1;

    LOG_INFO("flow 5/14 opening audio file for session {}", session_id_);
    if (!open_audio_context(file_path_))
    {
        LOG_ERROR("init audio context failed {}", file.toStdString());
        emit decoding_error("Failed to open audio file.");
        return;
    }

    stop_flag_ = false;

    if (offset > 0)
    {
        seek(session_id_, offset);
    }

    LOG_INFO("session {} decoder probed file successfully and is now waiting for resume signal", session_id_);
}

void audio_decoder::resume_decoding()
{
    if (stop_flag_)
    {
        LOG_WARN("resume decoding requested but decoder is stopped");
        return;
    }
    LOG_INFO("flow 13/14 & 10/10 received resume signal starting decoding cycle for session {}", session_id_);
    QMetaObject::invokeMethod(this, "do_decoding_cycle", Qt::QueuedConnection);
}

void audio_decoder::do_decoding_cycle()
{
    if (stop_flag_)
    {
        LOG_DEBUG("decoding stopped by request");
        close_audio_context();
        emit decoding_finished();
        return;
    }

    while (!stop_flag_)
    {
        int receive_ret = avcodec_receive_frame(codec_ctx_, frame_);

        if (receive_ret == 0)
        {
            process_frame(frame_);
            return;
        }

        if (receive_ret == AVERROR_EOF)
        {
            LOG_INFO("flow end 1/4 finished decoding file for session {} sending eof signal", session_id_);
            emit packet_ready(session_id_, nullptr);
            LOG_INFO("session {} end of file reached, decoder is now idle", session_id_);
            return;
        }

        if (receive_ret != AVERROR(EAGAIN))
        {
            LOG_ERROR("receive frame failed {}", ffmpeg_error_string(receive_ret));
            break;
        }

        int read_ret = av_read_frame(format_ctx_, packet_);
        if (read_ret < 0)
        {
            avcodec_send_packet(codec_ctx_, nullptr);
            continue;
        }

        DEFER(av_packet_unref(packet_));
        if (packet_->stream_index != audio_stream_index_)
        {
            continue;
        }

        int send_ret = avcodec_send_packet(codec_ctx_, packet_);
        if (send_ret < 0)
        {
            LOG_WARN("send packet failed {}", ffmpeg_error_string(send_ret));
            continue;
        }
    }
}

void audio_decoder::do_seek()
{
    LOG_INFO("flow seek 4/10 is executing seek for session {}", seek_session_id_);
    if (!seek_requested_)
    {
        return;
    }

    if (format_ctx_ == nullptr)
    {
        LOG_WARN("session {} seek ignored because audio context is not open", seek_session_id_);
        emit seek_finished(seek_session_id_, -1);
        return;
    }

    qint64 current_seek_id = seek_session_id_;
    qint64 target_pos_ms = seek_position_ms_;
    qint64 seek_target_ts = av_rescale_q(target_pos_ms, {1, 1000}, time_base_);
    int ret = av_seek_frame(format_ctx_, audio_stream_index_, seek_target_ts, AVSEEK_FLAG_BACKWARD);

    seek_requested_ = false;

    if (ret >= 0)
    {
        LOG_INFO("session {} seek to {}ms successful", current_seek_id, target_pos_ms);
        avcodec_flush_buffers(codec_ctx_);
        LOG_INFO("flow seek 5/10 notifying mainwindow of successful seek for session {}", current_seek_id);
        emit seek_finished(current_seek_id, target_pos_ms);
    }
    else
    {
        LOG_WARN("session {} seek to {}ms failed {}", current_seek_id, target_pos_ms, ffmpeg_error_string(ret));
        LOG_INFO("flow seek 5/10 notifying mainwindow of failed seek for session {}", current_seek_id);
        emit seek_finished(current_seek_id, -1);
    }
}

void audio_decoder::process_frame(AVFrame* frame)
{
    int converted_samples = swr_convert(swr_ctx_, &swr_data_, target_format_.sampleRate(), (const uint8_t**)frame->data, frame->nb_samples);
    if (converted_samples < 0)
    {
        LOG_ERROR("convert failed {}", ffmpeg_error_string(converted_samples));
        return;
    }

    int buffer_size = av_samples_get_buffer_size(&swr_data_linesize_, target_format_.channelCount(), converted_samples, target_ffmpeg_fmt_, 1);

    qint64 timestamp_ms = 0;
    if (frame->pts != AV_NOPTS_VALUE)
    {
        timestamp_ms = static_cast<qint64>(av_q2d(time_base_) * 1000 * static_cast<double>(frame->pts));
    }
    LOG_TRACE("session {} decoded packet data len {} pts {} ms {}", session_id_, buffer_size, frame->pts, timestamp_ms);
    if (buffer_size > 0)
    {
        auto packet = std::make_shared<audio_packet>();
        packet->data.assign(swr_data_, swr_data_ + buffer_size);
        packet->ms = timestamp_ms;
        emit packet_ready(session_id_, packet);
    }
}

bool audio_decoder::open_audio_context(const QString& file_path)
{
    auto guard = make_scoped_exit([this]() { close_audio_context(); });

    format_ctx_ = avformat_alloc_context();
    int ret = avformat_open_input(&format_ctx_, file_path.toUtf8().constData(), nullptr, nullptr);
    if (ret != 0)
    {
        LOG_ERROR("open input failed {} {}", file_path.toStdString(), ffmpeg_error_string(ret));
        return false;
    }
    LOG_INFO("open input file {}", file_path.toStdString());

    ret = avformat_find_stream_info(format_ctx_, nullptr);
    if (ret < 0)
    {
        LOG_ERROR("find stream info failed {}", ffmpeg_error_string(ret));
        return false;
    }

    audio_stream_index_ = av_find_best_stream(format_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_stream_index_ < 0)
    {
        LOG_ERROR("could not find audio stream in file {}", file_path.toStdString());
        return false;
    }
    LOG_INFO("find stream info audio index {}", audio_stream_index_);

    AVStream* audio_stream = format_ctx_->streams[audio_stream_index_];
    time_base_ = audio_stream->time_base;
    av_dump_format(format_ctx_, audio_stream_index_, nullptr, 0);

    const AVCodec* codec = avcodec_find_decoder(audio_stream->codecpar->codec_id);
    if (codec == nullptr)
    {
        LOG_ERROR("not found codec for id {}", (int)audio_stream->codecpar->codec_id);
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (codec_ctx_ == nullptr)
    {
        LOG_ERROR("could not allocate codec context");
        return false;
    }

    ret = avcodec_parameters_to_context(codec_ctx_, audio_stream->codecpar);
    if (ret < 0)
    {
        LOG_ERROR("codec parameters to context failed {}", ffmpeg_error_string(ret));
        return false;
    }
    LOG_INFO("codec parameters to context success {}", codec->name);

    if (avcodec_open2(codec_ctx_, codec, nullptr) < 0)
    {
        LOG_ERROR("open codec failed {} {}", codec->name, ffmpeg_error_string(ret));
        return false;
    }
    LOG_INFO("open codec success {}", codec->name);

    target_format_.setSampleRate(codec_ctx_->sample_rate);
    target_format_.setChannelCount(codec_ctx_->ch_layout.nb_channels);

    AVSampleFormat target_sample_fmt = get_av_sample_format(target_format_.sampleFormat());
    int target_sample_rate = target_format_.sampleRate();
    AVChannelLayout target_ch_layout;
    av_channel_layout_default(&target_ch_layout, target_format_.channelCount());

    LOG_INFO("configuring resampler src rate {} fmt {} layout {} dst rate {} fmt {} layout {}",
             codec_ctx_->sample_rate,
             av_get_sample_fmt_name(codec_ctx_->sample_fmt),
             codec_ctx_->ch_layout.nb_channels,
             target_sample_rate,
             av_get_sample_fmt_name(target_sample_fmt),
             target_format_.channelCount());

    ret = swr_alloc_set_opts2(&swr_ctx_,
                              &target_ch_layout,
                              target_sample_fmt,
                              target_sample_rate,
                              &codec_ctx_->ch_layout,
                              codec_ctx_->sample_fmt,
                              codec_ctx_->sample_rate,
                              0,
                              nullptr);
    if (ret != 0)
    {
        LOG_ERROR("swr alloc set opts failed {}", ffmpeg_error_string(ret));
        return false;
    }

    ret = swr_init(swr_ctx_);
    if (ret != 0)
    {
        LOG_ERROR("swr init failed {}", ffmpeg_error_string(ret));
        return false;
    }
    AVSampleFormat target_fmt = get_av_sample_format(target_format_.sampleFormat());

    int alloc_ret = av_samples_alloc(&swr_data_, &swr_data_linesize_, target_format_.channelCount(), target_format_.sampleRate(), target_fmt, 0);
    if (alloc_ret < 0)
    {
        LOG_ERROR("sample alloc failed {}", ffmpeg_error_string(alloc_ret));
        return false;
    }

    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (frame_ == nullptr || packet_ == nullptr)
    {
        LOG_ERROR("frame or packet alloc failed");
        return false;
    }

    if (format_ctx_->duration != AV_NOPTS_VALUE)
    {
        qint64 duration_ms = format_ctx_->duration / (AV_TIME_BASE / 1000);
        LOG_INFO("flow 6/14 notifying mainwindow with audio info for session {}", session_id_);
        emit duration_ready(session_id_, duration_ms, target_format_);
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
