#include "log.h"
#include "scoped_exit.h"
#include "audio_decoder_thread.h"

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

audio_decoder::audio_decoder(QObject* parent) : QThread(parent), stop_flag_(false) {}

audio_decoder::~audio_decoder()
{
    if (isRunning())
    {
        stop();
        wait();
    }
}

void audio_decoder::set_data_callback(const pcm_data_callback& callback) { data_callback_ = callback; }

void audio_decoder::start_decoding(const QString& file_path, const QAudioFormat& target_format)
{
    if (isRunning())
    {
        stop();
        wait();
    }
    stop_flag_ = false;
    file_path_ = file_path;
    target_format_ = target_format;
    start();
}

void audio_decoder::stop() { stop_flag_ = true; }

void audio_decoder::run()
{
    if (!init_ffmpeg(file_path_))
    {
        LOG_ERROR("init ffmpeg failed");
        emit decoding_finished();
        return;
    }

    AVSampleFormat target_fmt = get_av_sample_format(target_format_.sampleFormat());

    const int max_dst_nb_samples = target_format_.sampleRate();
    uint8_t* dst_data = nullptr;
    int dst_linesize = 0;
    int alloc_ret = av_samples_alloc(&dst_data, &dst_linesize, target_format_.channelCount(), max_dst_nb_samples, target_fmt, 0);
    if (alloc_ret < 0)
    {
        LOG_ERROR("sample alloc failed {}", ffmpeg_error_string(alloc_ret));
        cleanup();
        emit decoding_finished();
        return;
    }

    AVFrame* frame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();

    auto guard = make_scoped_exit([&]() { cleanup(); });

    while (!stop_flag_ && av_read_frame(format_ctx_, packet) >= 0)
    {
        DEFER(av_packet_unref(packet));
        if (packet->stream_index != audio_stream_index_)
        {
            continue;
        }

        int ret = avcodec_send_packet(codec_ctx_, packet);
        if (ret < 0)
        {
            LOG_WARN("send packet failed {}", ffmpeg_error_string(ret));
            continue;
        }

        while (!stop_flag_ && ret >= 0)
        {
            ret = avcodec_receive_frame(codec_ctx_, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                break;
            }
            if (ret < 0)
            {
                LOG_ERROR("recieve frame failed {}", ffmpeg_error_string(ret));
                break;
            }

            int converted_samples = swr_convert(swr_ctx_, &dst_data, max_dst_nb_samples, (const uint8_t**)frame->data, frame->nb_samples);
            if (converted_samples < 0)
            {
                LOG_ERROR("convert failed {}", ffmpeg_error_string(converted_samples));
                continue;
            }

            int buffer_size = av_samples_get_buffer_size(&dst_linesize, target_format_.channelCount(), converted_samples, target_fmt, 1);

            qint64 timestamp_ms = 0;
            if (frame->pts != AV_NOPTS_VALUE)
            {
                timestamp_ms = static_cast<qint64>(av_q2d(time_base_) * 1000 * static_cast<double>(frame->pts));
            }
            LOG_TRACE("data len {} pts {} ms {}", buffer_size, frame->pts, timestamp_ms);

            if (data_callback_ && buffer_size > 0)
            {
                data_callback_(dst_data, buffer_size, timestamp_ms);
            }
        }
    }
    if (dst_data != nullptr)
    {
        av_freep(static_cast<void*>(&dst_data));
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    emit decoding_finished();
}

bool audio_decoder::init_ffmpeg(const QString& file_path)
{
    auto guard = make_scoped_exit([this]() { cleanup(); });

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
        return false;
    }
    LOG_INFO("find stram info audio index {}", audio_stream_index_);

    AVStream* audio_stream = format_ctx_->streams[audio_stream_index_];
    time_base_ = audio_stream->time_base;
    av_dump_format(format_ctx_, audio_stream_index_, nullptr, 0);
    if (format_ctx_->duration != AV_NOPTS_VALUE)
    {
        qint64 duration_ms = format_ctx_->duration / (AV_TIME_BASE / 1000);
        LOG_INFO("{} audio duration {}", file_path.toStdString(), duration_ms);
        emit duration_ready(duration_ms);
    }
    const auto* codec_name = avcodec_get_name(audio_stream->codecpar->codec_id);
    if (codec_name == nullptr)
    {
        LOG_ERROR("not found codec name");
        return false;
    }
    const AVCodec* codec = avcodec_find_decoder(audio_stream->codecpar->codec_id);
    if (codec == nullptr)
    {
        LOG_ERROR("not found codec {}", codec_name);
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    ret = avcodec_parameters_to_context(codec_ctx_, audio_stream->codecpar);
    if (ret < 0)
    {
        LOG_ERROR("codec parameters to context failed {}", ffmpeg_error_string(ret));
        return false;
    }
    LOG_INFO("codec parameters to context success {}", codec_name);
    if (avcodec_open2(codec_ctx_, codec, nullptr) < 0)
    {
        LOG_ERROR("open codec failed {} {}", codec_name, ffmpeg_error_string(ret));
        return false;
    }
    LOG_INFO("open codec success {}", codec_name);

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
    guard.cancel();
    return true;
}

void audio_decoder::cleanup()
{
    if (codec_ctx_ != nullptr)
    {
        avcodec_free_context(&codec_ctx_);
    }
    if (format_ctx_ != nullptr)
    {
        avformat_close_input(&format_ctx_);
    }
    if (nullptr != swr_ctx_)
    {
        swr_free(&swr_ctx_);
    }

    format_ctx_ = nullptr;
    codec_ctx_ = nullptr;
    swr_ctx_ = nullptr;
}
