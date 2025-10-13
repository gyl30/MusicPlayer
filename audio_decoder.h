#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include <QObject>
#include <QAudioFormat>
#include <atomic>
#include <memory>
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
    explicit audio_decoder(QObject* parent = nullptr);
    ~audio_decoder() override;

   public slots:
    void start_decoding(qint64 session_id, const QString& file, const QAudioFormat& fmt, qint64 offset = -1);
    void resume_decoding();
    void shutdown();
    void seek(qint64 session_id, qint64 position_ms);

   signals:
    void duration_ready(qint64 session_id, qint64 duration_ms, const QAudioFormat& format);
    void packet_ready(qint64 session_id, const std::shared_ptr<audio_packet>& packet);
    void decoding_finished();
    void seek_finished(qint64 session_id, qint64 actual_seek_ms);
    void decoding_error(const QString& error_message);

   private slots:
    void do_seek();
    void do_decoding_cycle();

   private:
    bool open_audio_context(const QString& file_path);
    void close_audio_context();
    void process_frame(AVFrame* frame);

   private:
    QString file_path_;
    std::atomic<bool> stop_flag_{true};
    qint64 session_id_ = 0;

    bool seek_requested_ = false;
    qint64 seek_position_ms_ = -1;
    qint64 seek_session_id_ = 0;

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
