#include <QMetaObject>
#include <QThread>
#include <QFileInfo>
#include "log.h"
#include "audio_player.h"
#include "audio_decoder.h"
#include "playback_controller.h"

playback_controller::playback_controller(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<std::shared_ptr<audio_packet>>("std::shared_ptr<audio_packet>");
    qRegisterMetaType<QMap<QString, QString>>("QMap<QString, QString>");
    qRegisterMetaType<QByteArray>("QByteArray");
    qRegisterMetaType<QList<LyricLine>>("QList<LyricLine>");
    qRegisterMetaType<playback_mode>("playback_mode");

    decoder_thread_ = new QThread(this);
    decoder_ = new audio_decoder();
    decoder_->moveToThread(decoder_thread_);

    connect(decoder_, &audio_decoder::duration_ready, this, &playback_controller::on_duration_ready, Qt::QueuedConnection);
    connect(decoder_, &audio_decoder::seek_finished, this, &playback_controller::on_decoder_seek_finished, Qt::QueuedConnection);
    connect(decoder_, &audio_decoder::decoding_error, this, &playback_controller::on_decoding_error, Qt::QueuedConnection);
    connect(decoder_, &audio_decoder::metadata_ready, this, &playback_controller::on_metadata_ready, Qt::QueuedConnection);
    connect(decoder_, &audio_decoder::cover_art_ready, this, &playback_controller::on_cover_art_ready, Qt::QueuedConnection);
    connect(decoder_, &audio_decoder::lyrics_ready, this, &playback_controller::on_lyrics_ready, Qt::QueuedConnection);

    connect(decoder_thread_, &QThread::finished, decoder_, &QObject::deleteLater);

    decoder_thread_->start();
    LOG_DEBUG("播放控制器已初始化");
}
playback_controller::~playback_controller()
{
    stop();
    decoder_thread_->quit();
    decoder_thread_->wait();
    LOG_DEBUG("播放控制器已销毁");
}

void playback_controller::play_file(const QString& file_path, qint64 start_position_ms)
{
    stop();
    if (!QFileInfo(file_path).isFile())
    {
        LOG_WARN("文件不存在 file {}", file_path.toStdString());
        emit file_unavailable(file_path);
        return;
    }
    is_paused_ = false;
    playback_start_position_ms_ = qMax<qint64>(0, start_position_ms);
    current_session_id_ = ++session_id_counter_;
    LOG_INFO("开始播放 session {} file {} start_ms {}", current_session_id_, file_path.toStdString(), playback_start_position_ms_);

    QFileInfo file_info(file_path);
    QString file_name = file_info.fileName();
    emit playback_started(file_path, file_name);

    QMetaObject::invokeMethod(decoder_,
                              "start_decoding",
                              Qt::QueuedConnection,
                              Q_ARG(qint64, current_session_id_),
                              Q_ARG(QString, file_path));
}

void playback_controller::stop()
{
    if (!is_media_loaded_ && !is_playing_ && current_session_id_ == 0 && player_thread_ == nullptr)
    {
        return;
    }
    LOG_INFO("停止播放 session {}", current_session_id_);
    is_playing_ = false;
    is_media_loaded_ = false;
    is_paused_ = false;

    if (player_ != nullptr)
    {
        player_->request_stop();
    }
    QMetaObject::invokeMethod(decoder_, "shutdown", Qt::BlockingQueuedConnection);

    cleanup_player();

    total_duration_ms_ = 0;
    last_position_ms_ = 0;
    is_seeking_ = false;
    pending_seek_ms_ = -1;
    playback_start_position_ms_ = 0;
    current_session_id_ = 0;
}

void playback_controller::pause_resume()
{
    if (!is_media_loaded_)
    {
        LOG_DEBUG("忽略暂停或恢复 媒体未加载");
        return;
    }
    is_paused_ = !is_paused_;

    if (is_paused_)
    {
        LOG_INFO("暂停 session {}", current_session_id_);
        QMetaObject::invokeMethod(player_, "pause_feeding", Qt::QueuedConnection, Q_ARG(qint64, current_session_id_));
    }
    else
    {
        LOG_INFO("恢复 session {}", current_session_id_);
        QMetaObject::invokeMethod(player_, "resume_feeding", Qt::QueuedConnection, Q_ARG(qint64, current_session_id_));
    }
    emit playback_paused(is_paused_);
}

void playback_controller::seek(qint64 position_ms)
{
    if (!is_media_loaded_)
    {
        LOG_DEBUG("忽略跳转 媒体未加载 target_ms {}", position_ms);
        return;
    }

    if (is_seeking_)
    {
        LOG_DEBUG("跳转排队 session {} target_ms {}", current_session_id_, position_ms);
        pending_seek_ms_ = position_ms;
        return;
    }

    is_seeking_ = true;

    if (player_ != nullptr)
    {
        QMetaObject::invokeMethod(player_, "pause_feeding", Qt::QueuedConnection, Q_ARG(qint64, current_session_id_));
    }

    LOG_INFO("跳转 session {} target_ms {}", current_session_id_, position_ms);
    QMetaObject::invokeMethod(decoder_, "seek", Qt::QueuedConnection, Q_ARG(qint64, current_session_id_), Q_ARG(qint64, position_ms));
}

void playback_controller::set_volume(int volume_percent)
{
    cached_volume_ = volume_percent;
    if (player_ != nullptr)
    {
        QMetaObject::invokeMethod(player_, "set_volume", Qt::QueuedConnection, Q_ARG(int, volume_percent));
    }
}

void playback_controller::on_duration_ready(qint64 session_id, qint64 duration_ms, const QAudioFormat& format)
{
    if (session_id != current_session_id_)
    {
        LOG_DEBUG("忽略过期会话的音频信息 session {} current {}", session_id, current_session_id_);
        return;
    }
    LOG_DEBUG("收到音频信息 session {} duration_ms {}", session_id, duration_ms);
    total_duration_ms_ = duration_ms;
    is_media_loaded_ = true;
    emit track_info_ready(duration_ms);

    cleanup_player();

    player_thread_ = new QThread(this);
    player_ = new audio_player();
    player_->set_volume(cached_volume_);

    player_->moveToThread(player_thread_);

    connect(decoder_, &audio_decoder::packet_ready, player_, &audio_player::enqueue_packet, Qt::BlockingQueuedConnection);
    connect(player_, &audio_player::progress_update, this, &playback_controller::on_progress_update, Qt::QueuedConnection);
    connect(player_, &audio_player::playback_finished, this, &playback_controller::on_playback_completed, Qt::QueuedConnection);
    connect(player_, &audio_player::playback_ready, this, &playback_controller::on_player_ready, Qt::QueuedConnection);
    connect(player_, &audio_player::playback_error, this, &playback_controller::on_player_error, Qt::QueuedConnection);
    connect(player_, &audio_player::seek_handled, this, &playback_controller::on_player_seek_handled, Qt::QueuedConnection);
    connect(player_, &audio_player::buffer_level_low, this, &playback_controller::on_buffer_level_low, Qt::QueuedConnection);
    // pause_decoding 只修改互斥保护的状态，必须在生产下一包前施加背压。
    connect(player_, &audio_player::buffer_level_high, decoder_, &audio_decoder::pause_decoding, Qt::DirectConnection);
    connect(player_thread_, &QThread::finished, player_, &QObject::deleteLater);

    player_thread_->start();
    player_thread_->setPriority(QThread::TimeCriticalPriority);

    QMetaObject::invokeMethod(player_, "start_playback", Qt::QueuedConnection,
                              Q_ARG(qint64, session_id), Q_ARG(QAudioFormat, format));
}

void playback_controller::on_player_ready(qint64 session_id)
{
    if (session_id != current_session_id_ || !is_media_loaded_ || is_seeking_)
    {
        return;
    }

    if (playback_start_position_ms_ > 0)
    {
        const qint64 position_ms = total_duration_ms_ > 0
                                      ? qMin(playback_start_position_ms_, total_duration_ms_)
                                      : playback_start_position_ms_;
        playback_start_position_ms_ = 0;
        seek(position_ms);
        return;
    }

    LOG_DEBUG("播放器就绪 session {}", session_id);
    resume_decoding(session_id);
}

void playback_controller::resume_decoding(qint64 session_id)
{
    if (session_id != current_session_id_ || !is_media_loaded_ || is_seeking_)
    {
        return;
    }
    LOG_DEBUG("开始填充缓冲区 session {}", session_id);
    is_playing_ = true;
    QMetaObject::invokeMethod(decoder_, "resume_decoding", Qt::QueuedConnection);
}

void playback_controller::on_player_error(qint64 session_id, const QString& error_message)
{
    if (session_id != current_session_id_)
    {
        return;
    }
    LOG_ERROR("播放器错误 session {} error {}", session_id, error_message.toStdString());
    emit playback_error(error_message);
    stop();
}

void playback_controller::on_decoding_error(qint64 session_id, const QString& error_message)
{
    if (session_id != current_session_id_)
    {
        return;
    }
    LOG_ERROR("解码器错误 session {} error {}", session_id, error_message.toStdString());
    emit playback_error(error_message);
    stop();
}

void playback_controller::on_buffer_level_low(qint64 session_id)
{
    if (session_id != current_session_id_ || !is_playing_ || is_seeking_)
    {
        return;
    }
    LOG_TRACE("缓冲区低水位 继续解码 session {}", session_id);
    QMetaObject::invokeMethod(decoder_, "resume_decoding", Qt::QueuedConnection);
}

void playback_controller::on_progress_update(qint64 session_id, qint64 current_ms)
{
    if (session_id != current_session_id_ || !is_playing_ || is_paused_ || is_seeking_)
    {
        return;
    }
    last_position_ms_ = current_ms;
    emit progress_updated(current_ms, total_duration_ms_);
}

void playback_controller::on_playback_completed(qint64 session_id)
{
    if (session_id != current_session_id_)
    {
        LOG_DEBUG("忽略过期会话的播放结束 session {} current {}", session_id, current_session_id_);
        return;
    }
    LOG_INFO("播放完成 session {} position_ms {} duration_ms {}", session_id, last_position_ms_, total_duration_ms_);
    is_playing_ = false;
    is_media_loaded_ = false;
    emit playback_finished();
}

void playback_controller::on_decoder_seek_finished(qint64 session_id, qint64 actual_seek_ms)
{
    if (session_id != current_session_id_ || !is_media_loaded_ || !is_seeking_)
    {
        return;
    }
    if (actual_seek_ms < 0)
    {
        LOG_WARN("跳转失败 session {}", session_id);
        is_seeking_ = false;
        pending_seek_ms_ = -1;
        resume_decoding(session_id);
        if (!is_paused_ && player_ != nullptr)
        {
            QMetaObject::invokeMethod(player_, "resume_feeding", Qt::QueuedConnection, Q_ARG(qint64, session_id));
        }
        emit seek_finished(false);
        return;
    }

    LOG_DEBUG("解码器跳转完成 session {} actual_ms {}", session_id, actual_seek_ms);
    emit seek_completed(actual_seek_ms);
    QMetaObject::invokeMethod(player_, "handle_seek", Qt::QueuedConnection,
                              Q_ARG(qint64, session_id), Q_ARG(qint64, actual_seek_ms));
}

void playback_controller::on_player_seek_handled(qint64 session_id)
{
    if (session_id != current_session_id_ || !is_media_loaded_ || !is_seeking_)
    {
        return;
    }
    is_seeking_ = false;
    if (pending_seek_ms_ >= 0)
    {
        const qint64 position_ms = pending_seek_ms_;
        pending_seek_ms_ = -1;
        seek(position_ms);
        return;
    }

    if (!is_paused_)
    {
        QMetaObject::invokeMethod(player_, "resume_feeding", Qt::QueuedConnection, Q_ARG(qint64, session_id));
    }
    resume_decoding(session_id);
    emit seek_finished(true);
}

void playback_controller::on_metadata_ready(qint64 session_id, const QMap<QString, QString>& metadata)
{
    if (session_id != current_session_id_)
    {
        return;
    }
    LOG_DEBUG("收到元数据 session {} tags {}", session_id, metadata.size());
    emit metadata_ready(metadata);
}

void playback_controller::on_cover_art_ready(qint64 session_id, const QByteArray& image_data)
{
    if (session_id != current_session_id_)
    {
        return;
    }
    LOG_DEBUG("收到封面 session {} bytes {}", session_id, image_data.size());
    emit cover_art_ready(image_data);
}

void playback_controller::on_lyrics_ready(qint64 session_id, const QList<LyricLine>& lyrics)
{
    if (session_id != current_session_id_)
    {
        return;
    }
    LOG_DEBUG("收到歌词 session {} lines {}", session_id, lyrics.size());
    emit lyrics_updated(lyrics);
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
        LOG_DEBUG("播放器线程已清理");
    }
}
