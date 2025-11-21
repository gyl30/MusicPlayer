#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include <QObject>
#include <QAudioFormat>
#include <atomic>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <QMap>
#include <QByteArray>
#include <QList>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
}

#include "audio_packet.h"

class audio_decoder : public QObject
{
    Q_OBJECT

   public:
    explicit audio_decoder(QObject* parent = nullptr);
    ~audio_decoder() override;

    bool is_aborted() const { return abort_request_.load(); }

   public slots:
    void start_decoding(qint64 session_id, const QString& file, qint64 offset = -1);
    void resume_decoding();
    void pause_decoding();
    void shutdown();
    void seek(qint64 session_id, qint64 position_ms);

   signals:
    void duration_ready(qint64 session_id, qint64 duration_ms, const QAudioFormat& format);
    void packet_ready(qint64 session_id, const std::shared_ptr<audio_packet>& packet);
    void decoding_finished();
    void seek_finished(qint64 session_id, qint64 actual_seek_ms);
    void decoding_error(const QString& error_message);
    void metadata_ready(qint64 session_id, const QMap<QString, QString>& metadata);
    void cover_art_ready(qint64 session_id, const QByteArray& image_data);
    void lyrics_ready(qint64 session_id, const QList<LyricLine>& lyrics);

   private:
    void decoding_loop();

    bool open_audio_context(const QString& file_path);
    void close_audio_context();
    void process_frame(AVFrame* frame);
    void process_metadata_and_lyrics(AVDictionary* container_metadata, AVDictionary* stream_metadata);

   private:
    QString file_path_;
    qint64 session_id_ = 0;

    std::thread worker_thread_;
    std::mutex state_mutex_;
    std::condition_variable wait_cv_;

    std::atomic<bool> is_running_{false};
    std::atomic<bool> abort_request_{false};
    bool is_paused_ = true;

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
    qint64 accumulated_ms_ = 0;

    qint64 start_time_offset_ms_ = 0;
    bool first_frame_processed_ = false;
};

#endif
