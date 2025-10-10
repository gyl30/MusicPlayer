#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include <QObject>
#include <QAudioFormat>
#include <functional>
#include <atomic>
#include <mutex>
#include "audio_packet.h"
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
}

class audio_decoder : public QObject
{
    Q_OBJECT

   public:
    using pcm_data_callback = std::function<void(const std::shared_ptr<audio_packet>&)>;

    explicit audio_decoder(QObject* parent = nullptr);
    ~audio_decoder() override;

   public:
    void startup(const QString& file, const QAudioFormat& fmt, qint64 offset = -1);
    void shutdown();
    void seek(qint64 position_ms);
    void set_data_callback(const pcm_data_callback& callback);

   signals:
    void duration_ready(qint64 duration_ms);
    void decoding_finished(const std::vector<std::shared_ptr<audio_packet>>& packets);

   private:
    bool open_audio_context(const QString& file_path);
    void close_audio_context();
    void process_frame(AVFrame* frame);
    void seek_ffmpeg();
    void send_packet();
    void recive_frame();
    void data_callback();

   private:
    int call_data_index_ = 0;
    pcm_data_callback data_callback_;
    QString file_path_;
    std::atomic<bool> stop_flag_{false};

    std::atomic<bool> seek_requested_{false};
    qint64 seek_position_ms_ = -1;
    std::mutex seek_mutex_;
    bool packet_cache_ok_ = true;
    std::vector<std::shared_ptr<audio_packet>> packet_cache_;
    AVFormatContext* format_ctx_ = nullptr;
    AVCodecContext* codec_ctx_ = nullptr;
    SwrContext* swr_ctx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    uint8_t* swr_data_ = nullptr;
    int swr_data_linesize_ = 0;
    int audio_stream_index_ = -1;
    AVRational time_base_;
    QAudioFormat target_format_;
    AVSampleFormat target_ffmpeg_fmt_ = AV_SAMPLE_FMT_NONE;
};

#endif
