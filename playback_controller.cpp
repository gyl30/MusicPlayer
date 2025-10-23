#include "playback_controller.h"
#include <QMetaObject>
#include <QThread>
#include <QFileInfo>
#include "log.h"
#include "audio_player.h"
#include "audio_decoder.h"
#include "spectrum_widget.h"

const static auto kBufferHighWatermarkSeconds = 5L;

playback_controller::playback_controller(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<std::shared_ptr<audio_packet>>("std::shared_ptr<audio_packet>");
    qRegisterMetaType<QMap<QString, QString>>("QMap<QString, QString>");
    qRegisterMetaType<QByteArray>("QByteArray");

    decoder_thread_ = new QThread(this);
    decoder_ = new audio_decoder();
    decoder_->moveToThread(decoder_thread_);

    connect(decoder_, &audio_decoder::duration_ready, this, &playback_controller::on_duration_ready, Qt::QueuedConnection);
    connect(decoder_, &audio_decoder::packet_ready, this, &playback_controller::on_packet_from_decoder, Qt::QueuedConnection);
    connect(decoder_, &audio_decoder::seek_finished, this, &playback_controller::on_decoder_seek_finished, Qt::QueuedConnection);
    connect(decoder_, &audio_decoder::decoding_error, this, &playback_controller::on_decoding_error, Qt::QueuedConnection);
    connect(decoder_, &audio_decoder::metadata_ready, this, &playback_controller::on_metadata_ready, Qt::QueuedConnection);
    connect(decoder_, &audio_decoder::cover_art_ready, this, &playback_controller::on_cover_art_ready, Qt::QueuedConnection);

    connect(decoder_thread_, &QThread::finished, decoder_, &QObject::deleteLater);

    decoder_thread_->start();
    LOG_INFO("播放控制器已初始化解码器线程已启动");
}
playback_controller::~playback_controller()
{
    stop();
    decoder_thread_->quit();
    decoder_thread_->wait();
    LOG_INFO("播放控制器已销毁");
}

void playback_controller::set_spectrum_widget(spectrum_widget* widget)
{
    spectrum_widget_ = widget;
    if (spectrum_widget_ != nullptr)
    {
        connect(
            spectrum_widget_, &spectrum_widget::playback_started, this, &playback_controller::on_spectrum_ready_for_decoding, Qt::QueuedConnection);
        LOG_INFO("已为播放控制器设置频谱部件");
    }
}

void playback_controller::play_file(const QString& file_path)
{
    LOG_INFO("播放流程二 控制中心收到文件播放请求");
    stop();
    is_paused_ = false;
    LOG_INFO("重置暂停状态");
    current_session_id_ = ++session_id_counter_;
    LOG_INFO("生成新会话id {}", current_session_id_);

    QFileInfo file_info(file_path);
    QString file_name = file_info.fileName();
    LOG_INFO("控制器发出playbackstarted信号");
    emit playback_started(file_path, file_name);

    LOG_INFO("播放流程三 通知解码器开始处理文件");
    QMetaObject::invokeMethod(
        decoder_, "start_decoding", Qt::QueuedConnection, Q_ARG(qint64, current_session_id_), Q_ARG(QString, file_path), Q_ARG(qint64, -1));
}

void playback_controller::stop()
{
    if (!is_media_loaded_ && !is_playing_)
    {
        return;
    }
    LOG_INFO("停止流程一 控制中心收到停止请求");
    is_playing_ = false;
    is_media_loaded_ = false;
    is_paused_ = false;
    LOG_INFO("重置暂停状态");
    buffered_bytes_ = 0;
    decoder_is_waiting_ = false;

    LOG_INFO("停止流程二 通知解码器关闭");
    QMetaObject::invokeMethod(decoder_, "shutdown", Qt::QueuedConnection);

    LOG_INFO("停止流程三 通知播放器停止并清理");
    cleanup_player();

    if (spectrum_widget_ != nullptr)
    {
        LOG_INFO("停止流程四 通知频谱部件停止");
        QMetaObject::invokeMethod(spectrum_widget_, "stop_playback", Qt::QueuedConnection);
    }
    total_duration_ms_ = 0;
    is_seeking_ = false;
    pending_seek_ms_ = -1;
    current_session_id_ = 0;
}

void playback_controller::pause_resume()
{
    if (!is_media_loaded_)
    {
        LOG_WARN("媒体未加载无法暂停或恢复");
        return;
    }
    is_paused_ = !is_paused_;

    if (is_paused_)
    {
        LOG_INFO("请求暂停播放会话id {}", current_session_id_);
        QMetaObject::invokeMethod(player_, "pause_feeding", Qt::QueuedConnection, Q_ARG(qint64, current_session_id_));
    }
    else
    {
        LOG_INFO("请求恢复播放会话id {}", current_session_id_);
        QMetaObject::invokeMethod(player_, "resume_feeding", Qt::QueuedConnection, Q_ARG(qint64, current_session_id_));
    }
}

void playback_controller::seek(qint64 position_ms)
{
    LOG_INFO("跳转流程二 控制中心收到跳转请求");
    if (!is_media_loaded_)
    {
        return;
    }

    if (is_seeking_)
    {
        LOG_INFO("跳转正忙将新的跳转请求置为待处理");
        pending_seek_ms_ = position_ms;
        return;
    }

    is_seeking_ = true;

    if (player_ != nullptr)
    {
        QMetaObject::invokeMethod(player_, "pause_feeding", Qt::QueuedConnection, Q_ARG(qint64, current_session_id_));
    }

    LOG_INFO("跳转流程三 通知解码器执行跳转");
    QMetaObject::invokeMethod(decoder_, "seek", Qt::QueuedConnection, Q_ARG(qint64, current_session_id_), Q_ARG(qint64, position_ms));
}

void playback_controller::set_volume(int volume_percent)
{
    if (player_ != nullptr)
    {
        QMetaObject::invokeMethod(player_, "set_volume", Qt::QueuedConnection, Q_ARG(int, volume_percent));
    }
}

void playback_controller::on_duration_ready(qint64 session_id, qint64 duration_ms, const QAudioFormat& format)
{
    if (session_id != current_session_id_)
    {
        LOG_WARN("忽略已过时会话的durationready信号");
        return;
    }
    LOG_INFO("播放流程六 从解码器收到音频信息");
    total_duration_ms_ = duration_ms;
    is_media_loaded_ = true;
    emit track_info_ready(duration_ms);

    cleanup_player();

    buffered_bytes_ = 0;
    decoder_is_waiting_ = false;
    buffer_high_water_mark_ = kBufferHighWatermarkSeconds * format.bytesPerFrame() * format.sampleRate();
    LOG_INFO("缓冲区高水位线设置为 {} 字节", buffer_high_water_mark_);

    player_thread_ = new QThread(this);
    player_ = new audio_player();
    player_->moveToThread(player_thread_);

    connect(player_, &audio_player::progress_update, this, &playback_controller::on_progress_update, Qt::QueuedConnection);
    connect(player_, &audio_player::playback_finished, this, &playback_controller::on_playback_finished, Qt::QueuedConnection);
    connect(player_, &audio_player::playback_ready, this, &playback_controller::on_player_ready_for_spectrum, Qt::QueuedConnection);
    connect(player_, &audio_player::playback_error, this, &playback_controller::on_player_error, Qt::QueuedConnection);
    connect(player_, &audio_player::packet_played, this, &playback_controller::on_packet_for_spectrum, Qt::QueuedConnection);
    connect(player_, &audio_player::seek_handled, this, &playback_controller::on_player_seek_handled, Qt::QueuedConnection);
    connect(player_, &audio_player::buffer_level_low, this, &playback_controller::on_buffer_level_low, Qt::QueuedConnection);
    connect(player_thread_, &QThread::finished, player_, &QObject::deleteLater);

    player_thread_->start();
    player_thread_->setPriority(QThread::TimeCriticalPriority);

    LOG_INFO("播放流程八 通知播放器准备启动");
    QMetaObject::invokeMethod(
        player_, "start_playback", Qt::QueuedConnection, Q_ARG(qint64, session_id), Q_ARG(QAudioFormat, format), Q_ARG(qint64, 0));
}

void playback_controller::on_player_ready_for_spectrum(qint64 session_id)
{
    if (session_id != current_session_id_ || spectrum_widget_ == nullptr)
    {
        return;
    }
    LOG_INFO("播放流程十 收到播放器的就绪信号");
    LOG_INFO("播放流程十一 通知频谱部件准备");
    QMetaObject::invokeMethod(spectrum_widget_, "reset_and_start", Qt::QueuedConnection, Q_ARG(qint64, session_id), Q_ARG(qint64, 0));
}

void playback_controller::on_spectrum_ready_for_decoding(qint64 session_id)
{
    if (session_id != current_session_id_)
    {
        return;
    }
    LOG_INFO("播放流程十二或跳转流程十 收到频谱部件的就绪信号");
    LOG_INFO("播放流程十三 数据链路建立完成通知解码器开始填充缓冲区");
    is_playing_ = true;
    QMetaObject::invokeMethod(decoder_, "resume_decoding", Qt::QueuedConnection);
}

void playback_controller::on_player_error(const QString& error_message)
{
    LOG_ERROR("收到播放器错误 {}", error_message.toStdString());
    emit playback_error(error_message);
    stop();
}

void playback_controller::on_decoding_error(const QString& error_message)
{
    LOG_ERROR("收到解码器错误 {}", error_message.toStdString());
    emit playback_error(error_message);
    stop();
}

void playback_controller::on_packet_from_decoder(qint64 session_id, const std::shared_ptr<audio_packet>& packet)
{
    if (session_id != current_session_id_)
    {
        return;
    }

    if (player_ == nullptr)
    {
        return;
    }

    if (packet)
    {
        buffered_bytes_ += static_cast<qint64>(packet->data.size());
    }
    else
    {
        LOG_INFO("结束流程二 从解码器收到文件结束信号转发至播放器");
    }

    QMetaObject::invokeMethod(
        player_, "enqueue_packet", Qt::QueuedConnection, Q_ARG(qint64, session_id), Q_ARG(std::shared_ptr<audio_packet>, packet));

    if (packet != nullptr && is_playing_ && !is_seeking_)
    {
        if (buffered_bytes_ < buffer_high_water_mark_)
        {
            QMetaObject::invokeMethod(decoder_, "resume_decoding", Qt::QueuedConnection);
        }
        else
        {
            decoder_is_waiting_ = true;
        }
    }
}

void playback_controller::on_packet_for_spectrum(const std::shared_ptr<audio_packet>& packet)
{
    if (spectrum_widget_ != nullptr && is_playing_)
    {
        buffered_bytes_ -= static_cast<qint64>(packet->data.size());
        spectrum_widget_->enqueue_packet(packet);
    }
}

void playback_controller::on_buffer_level_low(qint64 session_id)
{
    if (session_id != current_session_id_)
    {
        return;
    }
    if (decoder_is_waiting_ && is_playing_ && !is_seeking_)
    {
        decoder_is_waiting_ = false;
        QMetaObject::invokeMethod(decoder_, "resume_decoding", Qt::QueuedConnection);
    }
}

void playback_controller::on_progress_update(qint64 session_id, qint64 current_ms)
{
    if (session_id != current_session_id_ || !is_playing_)
    {
        return;
    }
    emit progress_updated(current_ms, total_duration_ms_);
}

void playback_controller::on_playback_finished(qint64 session_id)
{
    if (session_id != current_session_id_)
    {
        LOG_INFO("忽略已过时会话的播放结束信号");
        return;
    }
    LOG_INFO("结束流程三 从播放器收到播放完成信号");
    LOG_INFO("结束流程四 通知频谱部件停止");
    is_playing_ = false;
    if (spectrum_widget_ != nullptr)
    {
        QMetaObject::invokeMethod(spectrum_widget_, "stop_playback", Qt::QueuedConnection);
    }
    emit playback_finished();
}

void playback_controller::on_decoder_seek_finished(qint64 session_id, qint64 actual_seek_ms)
{
    if (session_id != current_session_id_)
    {
        LOG_WARN("忽略已过时会话的跳转完成信号");
        return;
    }
    LOG_INFO("跳转流程五 从解码器收到跳转结果实际位置 {}ms", actual_seek_ms);

    if (actual_seek_ms < 0)
    {
        LOG_WARN("跳转流程六 跳转失败恢复播放");
        is_seeking_ = false;
        pending_seek_ms_ = -1;
        if (is_playing_ && player_ != nullptr)
        {
            QMetaObject::invokeMethod(player_, "resume_feeding", Qt::QueuedConnection, Q_ARG(qint64, session_id));
        }
        emit seek_finished(false);
        return;
    }

    emit seek_completed(actual_seek_ms);

    if (total_duration_ms_ > 0 && total_duration_ms_ - actual_seek_ms < 250)
    {
        LOG_INFO("跳转结果已在文件末尾转换到结束状态");
        is_seeking_ = false;
        if (player_ != nullptr)
        {
            QMetaObject::invokeMethod(player_, "handle_seek", Qt::QueuedConnection, Q_ARG(qint64, session_id), Q_ARG(qint64, actual_seek_ms));
        }
        on_playback_finished(session_id);
        return;
    }

    LOG_INFO("跳转流程七 通知播放器处理跳转");
    buffered_bytes_ = 0;
    decoder_is_waiting_ = false;
    seek_result_ms_ = actual_seek_ms;

    if (player_ != nullptr)
    {
        QMetaObject::invokeMethod(player_, "handle_seek", Qt::QueuedConnection, Q_ARG(qint64, session_id), Q_ARG(qint64, actual_seek_ms));
    }
}

void playback_controller::on_player_seek_handled(qint64 session_id)
{
    if (session_id != current_session_id_)
    {
        return;
    }
    LOG_INFO("跳转流程八 收到播放器已处理跳转的信号");

    if (player_ != nullptr)
    {
        QMetaObject::invokeMethod(player_, "resume_feeding", Qt::QueuedConnection, Q_ARG(qint64, session_id));
    }
    LOG_INFO("跳转流程九 通知频谱部件为跳转重置");

    if (spectrum_widget_ != nullptr)
    {
        QMetaObject::invokeMethod(
            spectrum_widget_, "reset_and_start", Qt::QueuedConnection, Q_ARG(qint64, session_id), Q_ARG(qint64, seek_result_ms_));
    }
    emit seek_finished(true);

    if (pending_seek_ms_ != -1)
    {
        LOG_INFO("发现待处理的跳转请求现在开始执行");
        qint64 new_seek_pos = pending_seek_ms_;
        pending_seek_ms_ = -1;
        seek(new_seek_pos);
        return;
    }

    is_seeking_ = false;
}
void playback_controller::on_metadata_ready(qint64 session_id, const QMap<QString, QString>& metadata)
{
    if (session_id != current_session_id_)
    {
        return;
    }
    LOG_DEBUG("控制器收到元数据, 转发至UI");
    emit metadata_ready(metadata);
}

void playback_controller::on_cover_art_ready(qint64 session_id, const QByteArray& image_data)
{
    if (session_id != current_session_id_)
    {
        return;
    }
    LOG_DEBUG("控制器收到封面数据, 转发至UI");
    emit cover_art_ready(image_data);
}

void playback_controller::cleanup_player()
{
    if (player_thread_ != nullptr)
    {
        if (player_ != nullptr)
        {
            QMetaObject::invokeMethod(player_, "stop_playback", Qt::BlockingQueuedConnection);
        }
        player_thread_->quit();
        player_thread_->wait();
        player_thread_->deleteLater();
        player_thread_ = nullptr;
        player_ = nullptr;
        LOG_DEBUG("播放器和播放器线程已清理");
    }
}
