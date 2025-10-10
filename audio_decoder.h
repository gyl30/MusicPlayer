#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include <QObject>
#include <QAudioFormat>
#include <functional>
#include <atomic>
#include <mutex>

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
    using pcm_data_callback = std::function<void(const uint8_t*, size_t, int64_t)>;

    explicit audio_decoder(QObject* parent = nullptr);
    ~audio_decoder() override;

    void set_data_callback(const pcm_data_callback& callback);

   public slots:
    void do_decoding(const QString& file_path, const QAudioFormat& target_format, qint64 initial_seek_ms = -1);
    void stop();
    void seek(qint64 position_ms);

   signals:
    void duration_ready(qint64 duration_ms);
    void decoding_finished();

   private:
    bool init_ffmpeg(const QString& file_path);
    void cleanup();
    void process_frame(AVFrame* frame, AVSampleFormat target_fmt, uint8_t* dst_data, int& dst_linesize, int max_dst_nb_samples);

   private:
    pcm_data_callback data_callback_;
    QString file_path_;
    std::atomic<bool> stop_flag_{false};

    std::atomic<bool> seek_requested_{false};
    qint64 seek_position_ms_ = -1;
    std::mutex seek_mutex_;

    AVFormatContext* format_ctx_ = nullptr;
    AVCodecContext* codec_ctx_ = nullptr;
    SwrContext* swr_ctx_ = nullptr;
    int audio_stream_index_ = -1;
    AVRational time_base_;
    QAudioFormat target_format_;
};

#endif
