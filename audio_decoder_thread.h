#ifndef AUDIO_DECODER_THREAD_H
#define AUDIO_DECODER_THREAD_H

#include <QThread>
#include <functional>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
}

class audio_decoder : public QThread
{
    Q_OBJECT

   public:
    using pcm_data_callback = std::function<void(const uint8_t*, size_t, int64_t)>;

    explicit audio_decoder(QObject* parent = nullptr);
    ~audio_decoder();

    void set_data_callback(const pcm_data_callback& callback);
    void start_decoding(const QString& file_path);
    void stop();

   protected:
    void run() override;

   signals:
    void duration_ready(qint64 duration_ms);
    void decoding_finished();

   private:
    bool init_ffmpeg(const QString& file_path);
    void cleanup();

    pcm_data_callback data_callback_;
    QString file_path_;
    volatile bool stop_flag_;

    AVFormatContext* format_ctx_ = nullptr;
    AVCodecContext* codec_ctx_ = nullptr;
    SwrContext* swr_ctx_ = nullptr;
    int audio_stream_index_ = -1;
    AVRational time_base_;
};

#endif
