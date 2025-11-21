#include <QMetaObject>

#include "log.h"
#include "scoped_exit.h"
#include "audio_decoder.h"
#include "lyrics_parser.h"

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

static int decode_interrupt_cb(void* ctx)
{
    auto* decoder = static_cast<audio_decoder*>(ctx);
    return decoder->is_aborted() ? 1 : 0;
}

audio_decoder::audio_decoder(QObject* parent) : QObject(parent) {}

audio_decoder::~audio_decoder() { shutdown(); }

void audio_decoder::shutdown()
{
    abort_request_ = true;

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!is_running_)
        {
            return;
        }
        is_running_ = false;
        is_paused_ = false;
    }
    wait_cv_.notify_all();

    if (worker_thread_.joinable())
    {
        worker_thread_.join();
    }

    close_audio_context();
}

void audio_decoder::start_decoding(qint64 session_id, const QString& file, qint64 offset)
{
    shutdown();

    session_id_ = session_id;
    file_path_ = file;

    is_running_ = true;
    abort_request_ = false;

    if (offset >= 0)
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        seek_requested_ = true;
        seek_position_ms_ = offset;
        seek_session_id_ = session_id;
        is_paused_ = false;
    }
    else
    {
        is_paused_ = false;
    }

    accumulated_ms_ = 0;
    first_frame_processed_ = false;

    worker_thread_ = std::thread(&audio_decoder::decoding_loop, this);
}

void audio_decoder::resume_decoding()
{
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!is_running_)
        {
            return;
        }
        if (!is_paused_)
        {
            return;
        }

        is_paused_ = false;
    }
    wait_cv_.notify_one();
}

void audio_decoder::pause_decoding()
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!is_running_)
    {
        return;
    }
    is_paused_ = true;
}

void audio_decoder::seek(qint64 session_id, qint64 position_ms)
{
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        seek_session_id_ = session_id;
        seek_position_ms_ = position_ms;
        seek_requested_ = true;
        is_paused_ = false;
    }
    wait_cv_.notify_one();
}

void audio_decoder::decoding_loop()
{
    if (!open_audio_context(file_path_))
    {
        emit decoding_error("Failed to open audio file");
        return;
    }

    while (is_running_)
    {
        {
            std::unique_lock<std::mutex> lock(state_mutex_);

            wait_cv_.wait(lock, [this] { return (!is_paused_ || seek_requested_ || !is_running_); });

            if (!is_running_)
            {
                break;
            }
        }

        bool do_seek = false;
        qint64 target_ms = 0;
        qint64 target_sess = 0;

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (seek_requested_)
            {
                do_seek = true;
                target_ms = seek_position_ms_;
                target_sess = seek_session_id_;
                seek_requested_ = false;
            }
        }

        if (do_seek)
        {
            if (format_ctx_ != nullptr)
            {
                qint64 seek_target_ts = av_rescale_q(target_ms, {1, 1000}, time_base_);

                avcodec_flush_buffers(codec_ctx_);

                int ret = av_seek_frame(format_ctx_, audio_stream_index_, seek_target_ts, AVSEEK_FLAG_BACKWARD);
                qint64 actual_ms = target_ms;

                if (ret < 0)
                {
                    actual_ms = -1;
                }
                else
                {
                    first_frame_processed_ = false;
                    accumulated_ms_ = target_ms;
                }

                emit seek_finished(target_sess, actual_ms);
            }
            continue;
        }

        int receive_ret = avcodec_receive_frame(codec_ctx_, frame_);

        if (receive_ret == 0)
        {
            process_frame(frame_);
            continue;
        }

        if (receive_ret == AVERROR_EOF)
        {
            emit packet_ready(session_id_, nullptr);

            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                is_paused_ = true;
            }
            continue;
        }

        if (receive_ret != AVERROR(EAGAIN))
        {
            emit decoding_error(QString::fromStdString(ffmpeg_error_string(receive_ret)));
            break;
        }

        int read_ret = av_read_frame(format_ctx_, packet_);
        if (read_ret < 0)
        {
            if (read_ret == AVERROR_EOF)
            {
                avcodec_send_packet(codec_ctx_, nullptr);
                continue;
            }
            if (is_aborted())
            {
                break;
            }
            avcodec_send_packet(codec_ctx_, nullptr);
            continue;
        }

        DEFER(av_packet_unref(packet_));

        if (packet_->stream_index == audio_stream_index_)
        {
            int send_ret = avcodec_send_packet(codec_ctx_, packet_);
            if (send_ret < 0)
            {
                LOG_WARN("avcodec_send_packet failed {}", ffmpeg_error_string(send_ret));
            }
        }
    }
}

bool audio_decoder::open_audio_context(const QString& file_path)
{
    close_audio_context();
    auto guard = make_scoped_exit([this]() { close_audio_context(); });

    format_ctx_ = avformat_alloc_context();
    if (format_ctx_ == nullptr)
    {
        return false;
    }

    format_ctx_->interrupt_callback.callback = decode_interrupt_cb;
    format_ctx_->interrupt_callback.opaque = this;
    abort_request_ = false;

    int ret = avformat_open_input(&format_ctx_, file_path.toUtf8().constData(), nullptr, nullptr);
    if (ret != 0)
    {
        LOG_ERROR("Failed to open input {} {}", file_path.toStdString(), ffmpeg_error_string(ret));
        return false;
    }

    ret = avformat_find_stream_info(format_ctx_, nullptr);
    if (ret < 0)
    {
        LOG_ERROR("Failed to find stream info {}", ffmpeg_error_string(ret));
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
                emit cover_art_ready(session_id_, image_data);
                break;
            }
        }
    }

    audio_stream_index_ = av_find_best_stream(format_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_stream_index_ < 0)
    {
        return false;
    }

    AVStream* audio_stream = format_ctx_->streams[audio_stream_index_];
    time_base_ = audio_stream->time_base;

    process_metadata_and_lyrics(format_ctx_->metadata, audio_stream->metadata);

    const AVCodec* codec = avcodec_find_decoder(audio_stream->codecpar->codec_id);
    if (codec == nullptr)
    {
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (codec_ctx_ == nullptr)
    {
        return false;
    }

    if (avcodec_parameters_to_context(codec_ctx_, audio_stream->codecpar) < 0)
    {
        return false;
    }

    if (avcodec_open2(codec_ctx_, codec, nullptr) < 0)
    {
        return false;
    }

    target_format_.setSampleRate(codec_ctx_->sample_rate);
    target_format_.setChannelCount(codec_ctx_->ch_layout.nb_channels);
    target_format_.setSampleFormat(QAudioFormat::Int16);
    target_ffmpeg_fmt_ = get_av_sample_format(target_format_.sampleFormat());

    AVChannelLayout target_ch_layout;
    av_channel_layout_default(&target_ch_layout, target_format_.channelCount());

    ret = swr_alloc_set_opts2(&swr_ctx_,
                              &target_ch_layout,
                              target_ffmpeg_fmt_,
                              target_format_.sampleRate(),
                              &codec_ctx_->ch_layout,
                              codec_ctx_->sample_fmt,
                              codec_ctx_->sample_rate,
                              0,
                              nullptr);
    if (ret != 0)
    {
        return false;
    }
    if (swr_init(swr_ctx_) != 0)
    {
        return false;
    }

    int alloc_ret =
        av_samples_alloc(&swr_data_, &swr_data_linesize_, target_format_.channelCount(), target_format_.sampleRate(), target_ffmpeg_fmt_, 0);
    if (alloc_ret < 0)
    {
        return false;
    }

    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (frame_ == nullptr || packet_ == nullptr)
    {
        return false;
    }

    if (format_ctx_->duration != AV_NOPTS_VALUE)
    {
        qint64 duration_ms = format_ctx_->duration / (AV_TIME_BASE / 1000);
        emit duration_ready(session_id_, duration_ms, target_format_);
    }

    guard.cancel();
    return true;
}

void audio_decoder::process_frame(AVFrame* frame)
{
    if (frame->pts != AV_NOPTS_VALUE && !first_frame_processed_)
    {
        start_time_offset_ms_ = static_cast<qint64>(av_q2d(time_base_) * 1000 * static_cast<double>(frame->pts));
        first_frame_processed_ = true;
    }

    int converted_samples = swr_convert(swr_ctx_, &swr_data_, target_format_.sampleRate(), (const uint8_t**)frame->data, frame->nb_samples);
    if (converted_samples < 0)
    {
        return;
    }

    int buffer_size = av_samples_get_buffer_size(&swr_data_linesize_, target_format_.channelCount(), converted_samples, target_ffmpeg_fmt_, 1);

    qint64 timestamp_ms = 0;
    if (frame->pts != AV_NOPTS_VALUE)
    {
        auto raw_timestamp_ms = static_cast<qint64>(av_q2d(time_base_) * 1000 * static_cast<double>(frame->pts));
        timestamp_ms = raw_timestamp_ms - start_time_offset_ms_;
    }
    else
    {
        qint64 duration_ms = static_cast<qint64>(frame->nb_samples) * 1000 / codec_ctx_->sample_rate;
        timestamp_ms = accumulated_ms_ + duration_ms;
    }

    accumulated_ms_ = timestamp_ms;
    timestamp_ms = std::max<qint64>(timestamp_ms, 0);

    if (buffer_size > 0)
    {
        auto packet = std::make_shared<audio_packet>();
        packet->data.assign(swr_data_, swr_data_ + buffer_size);
        packet->ms = timestamp_ms;
        emit packet_ready(session_id_, packet);
    }
}

void audio_decoder::process_metadata_and_lyrics(AVDictionary* container_metadata, AVDictionary* stream_metadata)
{
    QMap<QString, QString> metadata_map;
    AVDictionaryEntry* tag = nullptr;

    auto populate_map = [&](AVDictionary* dict)
    {
        tag = nullptr;
        while ((tag = av_dict_get(dict, "", tag, AV_DICT_IGNORE_SUFFIX)) != nullptr)
        {
            QString key = QString::fromUtf8(tag->key);
            QString value = QString::fromUtf8(tag->value);
            metadata_map.insert(key, value);
        }
    };

    populate_map(container_metadata);
    populate_map(stream_metadata);

    QString lrc_text;
    bool lyrics_found = false;
    const QStringList lyrics_keys = {"lyrics", "USLT", "comment"};
    for (const QString& key_prefix : lyrics_keys)
    {
        for (auto it = metadata_map.constKeyValueBegin(); it != metadata_map.constKeyValueEnd(); ++it)
        {
            const QString& key = it->first;
            if (key.compare(key_prefix, Qt::CaseInsensitive) == 0 || (key_prefix == "lyrics" && key.startsWith("lyrics-", Qt::CaseInsensitive)))
            {
                lrc_text = it->second;
                lyrics_found = true;
                break;
            }
        }
        if (lyrics_found)
        {
            break;
        }
    }

    QList<LyricLine> parsed_lyrics = lyrics_parser::parse(lrc_text);
    emit lyrics_ready(session_id_, parsed_lyrics);

    if (!metadata_map.isEmpty())
    {
        emit metadata_ready(session_id_, metadata_map);
    }
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
