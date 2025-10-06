#include "log.h"
#include "audio_decoder_thread.h"

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

void audio_decoder::start_decoding(const QString& file_path)
{
    if (isRunning())
    {
        stop();
        wait();
    }
    file_path_ = file_path;
    stop_flag_ = false;
    start();
}

void audio_decoder::stop() { stop_flag_ = true; }

void audio_decoder::run()
{
    if (!init_ffmpeg(file_path_))
    {
        LOG_ERROR("init ff failed");
        emit decoding_finished();
        return;
    }

    AVFrame* frame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();

    while (!stop_flag_ && av_read_frame(format_ctx_, packet) >= 0)
    {
        if (packet->stream_index != audio_stream_index_)
        {
            continue;
        }
        if (avcodec_send_packet(codec_ctx_, packet) == 0)
        {
            while (!stop_flag_ && avcodec_receive_frame(codec_ctx_, frame) == 0)
            {
                uint8_t* dst_data = nullptr;
                int dst_linesize;
                av_samples_alloc(&dst_data, &dst_linesize, frame->ch_layout.nb_channels, frame->nb_samples, AV_SAMPLE_FMT_S16, 0);

                swr_convert(swr_ctx_, &dst_data, frame->nb_samples, static_cast<uint8_t**>(frame->data), frame->nb_samples);
                int buffer_size = av_samples_get_buffer_size(&dst_linesize, frame->ch_layout.nb_channels, frame->nb_samples, AV_SAMPLE_FMT_S16, 1);

                qint64 timestamp_ms = 0;
                if (frame->pts != AV_NOPTS_VALUE)
                {
                    timestamp_ms = static_cast<qint64>(av_q2d(time_base_) * 1000 * static_cast<double>(frame->pts));
                }
                LOG_DEBUG("data len {} pts {} ms {}", buffer_size, frame->pts, timestamp_ms);
                if (data_callback_ && buffer_size > 0)
                {
                    data_callback_(dst_data, buffer_size, timestamp_ms);
                }

                av_freep(static_cast<void*>(&dst_data));
            }
        }
        av_packet_unref(packet);
    }

    av_frame_free(&frame);
    av_packet_free(&packet);

    cleanup();
    emit decoding_finished();
}

bool audio_decoder::init_ffmpeg(const QString& file_path)
{
    if (avformat_open_input(&format_ctx_, file_path.toUtf8().constData(), nullptr, nullptr) != 0)
    {
        return false;
    }
    LOG_DEBUG("open input file {}", file_path.toStdString());
    if (avformat_find_stream_info(format_ctx_, nullptr) < 0)
    {
        return false;
    }
    audio_stream_index_ = av_find_best_stream(format_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_stream_index_ < 0)
    {
        return false;
    }
    LOG_DEBUG("find stram info audio index {}", audio_stream_index_);

    AVStream* audio_stream = format_ctx_->streams[audio_stream_index_];
    time_base_ = audio_stream->time_base;
    av_dump_format(format_ctx_, audio_stream_index_, nullptr, 0);
    if (format_ctx_->duration != AV_NOPTS_VALUE)
    {
        qint64 duration_ms = format_ctx_->duration / (AV_TIME_BASE / 1000);
        emit duration_ready(duration_ms);
    }

    const AVCodec* codec = avcodec_find_decoder(audio_stream->codecpar->codec_id);
    if (codec == nullptr)
    {
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (avcodec_parameters_to_context(codec_ctx_, audio_stream->codecpar) < 0)
    {
        return false;
    }
    if (avcodec_open2(codec_ctx_, codec, nullptr) < 0)
    {
        return false;
    }

    AVChannelLayout target_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    swr_alloc_set_opts2(&swr_ctx_,
                        &target_ch_layout,
                        AV_SAMPLE_FMT_S16,
                        codec_ctx_->sample_rate,
                        &codec_ctx_->ch_layout,
                        codec_ctx_->sample_fmt,
                        codec_ctx_->sample_rate,
                        0,
                        nullptr);
    swr_init(swr_ctx_);

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
