#include <QThread>
#include <QMetaObject>
#include "log.h"
#include "audio_player.h"
#include "audio_decoder.h"
#include "spectrum_widget.h"
#include "playback_controller.h"

const static auto kBufferHighWatermarkSeconds = 5L;

playback_controller::playback_controller(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<std::shared_ptr<audio_packet>>("std::shared_ptr<audio_packet>");

    decoder_thread_ = new QThread(this);
    decoder_ = new audio_decoder();
    decoder_->moveToThread(decoder_thread_);

    connect(decoder_, &audio_decoder::duration_ready, this, &playback_controller::on_duration_ready, Qt::QueuedConnection);
    connect(decoder_, &audio_decoder::packet_ready, this, &playback_controller::on_packet_from_decoder, Qt::QueuedConnection);
    connect(decoder_, &audio_decoder::seek_finished, this, &playback_controller::on_decoder_seek_finished, Qt::QueuedConnection);
    connect(decoder_, &audio_decoder::decoding_error, this, &playback_controller::on_decoding_error, Qt::QueuedConnection);
    connect(decoder_thread_, &QThread::finished, decoder_, &QObject::deleteLater);

    decoder_thread_->start();
    LOG_INFO("playback controller initialized and decoder thread started");
}

playback_controller::~playback_controller()
{
    stop();
    decoder_thread_->quit();
    decoder_thread_->wait();
    LOG_INFO("playback controller destroyed");
}

void playback_controller::set_spectrum_widget(spectrum_widget* widget)
{
    spectrum_widget_ = widget;
    if (spectrum_widget_ != nullptr)
    {
        connect(
            spectrum_widget_, &spectrum_widget::playback_started, this, &playback_controller::on_spectrum_ready_for_decoding, Qt::QueuedConnection);
        LOG_INFO("spectrum widget set for playback controller");
    }
}

void playback_controller::play_file(const QString& file_path)
{
    LOG_INFO("flow 1 14 controller received play request for file {}", file_path.toStdString());
    stop();

    current_session_id_ = ++session_id_counter_;
    LOG_INFO("flow 3 14 notifying decoder to start new file {} for session {}", file_path.toStdString(), current_session_id_);
    QMetaObject::invokeMethod(decoder_,
                              "start_decoding",
                              Qt::QueuedConnection,
                              Q_ARG(qint64, current_session_id_),
                              Q_ARG(QString, file_path),
                              Q_ARG(qint64, -1));
}

void playback_controller::stop()
{
    if (!is_media_loaded_ && !is_playing_)
    {
        return;
    }
    LOG_INFO("session {} stopping playback and cleaning up resources", current_session_id_);
    is_playing_ = false;
    is_media_loaded_ = false;

    buffered_bytes_ = 0;
    decoder_is_waiting_ = false;

    QMetaObject::invokeMethod(decoder_, "shutdown", Qt::QueuedConnection);

    cleanup_player();

    if (spectrum_widget_ != nullptr)
    {
        QMetaObject::invokeMethod(spectrum_widget_, "stop_playback", Qt::QueuedConnection);
    }
    total_duration_ms_ = 0;
    is_seeking_ = false;
    pending_seek_ms_ = -1;
    current_session_id_ = 0;
}

void playback_controller::seek(qint64 position_ms)
{
    LOG_INFO("flow seek 1 10 controller received seek request to {}ms for session {}", position_ms, current_session_id_);
    if (!is_media_loaded_)
    {
        return;
    }

    if (is_seeking_)
    {
        LOG_INFO("session {} seek is busy pending new request to {}ms", current_session_id_, position_ms);
        pending_seek_ms_ = position_ms;
        return;
    }

    is_seeking_ = true;

    if (player_ != nullptr)
    {
        QMetaObject::invokeMethod(player_, "pause_feeding", Qt::QueuedConnection, Q_ARG(qint64, current_session_id_));
    }

    LOG_INFO("flow seek 3 10 notifying decoder to seek for session {}", current_session_id_);
    QMetaObject::invokeMethod(decoder_, "seek", Qt::QueuedConnection, Q_ARG(qint64, current_session_id_), Q_ARG(qint64, position_ms));
}

void playback_controller::on_duration_ready(qint64 session_id, qint64 duration_ms, const QAudioFormat& format)
{
    if (session_id != current_session_id_)
    {
        LOG_WARN("session {} ignoring duration_ready for obsolete session current is {}", session_id, current_session_id_);
        return;
    }
    LOG_INFO("flow 6 14 received audio info from decoder for session {}", session_id);
    total_duration_ms_ = duration_ms;
    is_media_loaded_ = true;
    emit track_info_ready(duration_ms);

    cleanup_player();

    buffered_bytes_ = 0;
    decoder_is_waiting_ = false;
    buffer_high_water_mark_ = kBufferHighWatermarkSeconds * format.bytesPerFrame() * format.sampleRate();
    LOG_INFO("session {} buffer high water mark set to {} bytes {} seconds", session_id, buffer_high_water_mark_, kBufferHighWatermarkSeconds);

    player_thread_ = new QThread(this);
    player_ = new audio_player();
    player_->moveToThread(player_thread_);

    connect(player_, &audio_player::progress_update, this, &playback_controller::on_progress_update, Qt::QueuedConnection);
    connect(player_, &audio_player::playback_finished, this, &playback_controller::on_playback_finished, Qt::QueuedConnection);
    connect(player_, &audio_player::playback_ready, this, &playback_controller::on_player_ready_for_spectrum, Qt::QueuedConnection);
    connect(player_, &audio_player::playback_error, this, &playback_controller::on_player_error, Qt::QueuedConnection);
    connect(player_, &audio_player::packet_played, this, &playback_controller::on_packet_for_spectrum, Qt::QueuedConnection);
    connect(player_, &audio_player::seek_handled, this, &playback_controller::on_player_seek_handled, Qt::QueuedConnection);
    connect(player_thread_, &QThread::finished, player_, &QObject::deleteLater);

    player_thread_->start();
    player_thread_->setPriority(QThread::TimeCriticalPriority);

    LOG_INFO("flow 8 14 notifying player to start for session {}", session_id);
    QMetaObject::invokeMethod(
        player_, "start_playback", Qt::QueuedConnection, Q_ARG(qint64, session_id), Q_ARG(QAudioFormat, format), Q_ARG(qint64, 0));
}

void playback_controller::on_player_ready_for_spectrum(qint64 session_id)
{
    if (session_id != current_session_id_ || spectrum_widget_ == nullptr)
    {
        return;
    }
    LOG_INFO("flow 10 14 received ready signal from player for session {}", session_id);
    LOG_INFO("flow 11 14 notifying spectrum to reset for session {}", session_id);
    QMetaObject::invokeMethod(spectrum_widget_, "reset_and_start", Qt::QueuedConnection, Q_ARG(qint64, session_id), Q_ARG(qint64, 0));
}

void playback_controller::on_spectrum_ready_for_decoding(qint64 session_id)
{
    if (session_id != current_session_id_)
    {
        return;
    }
    LOG_INFO("flow 12 14 & seek 10 10 received ready signal from spectrum for session {}", session_id);
    LOG_INFO("flow 13 14 & seek 10 10 notifying decoder to start decoding for session {}", session_id);
    is_playing_ = true;
    QMetaObject::invokeMethod(decoder_, "resume_decoding", Qt::QueuedConnection);
}

void playback_controller::on_player_error(const QString& error_message)
{
    LOG_ERROR("received player error {}", error_message.toStdString());
    emit playback_error(error_message);
    stop();
}

void playback_controller::on_decoding_error(const QString& error_message)
{
    LOG_ERROR("received decoder error {}", error_message.toStdString());
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
        LOG_INFO("flow end 2 4 received eof from decoder forwarding to player for session {}", session_id);
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
            LOG_TRACE("session {} buffer is full {} bytes decoder now waiting", session_id, buffered_bytes_.load());
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

        if (decoder_is_waiting_ && is_playing_ && !is_seeking_)
        {
            if (buffered_bytes_ < buffer_high_water_mark_)
            {
                LOG_TRACE("session {} buffer has space {} bytes waking up decoder", current_session_id_, buffered_bytes_.load());
                decoder_is_waiting_ = false;
                QMetaObject::invokeMethod(decoder_, "resume_decoding", Qt::QueuedConnection);
            }
        }
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
        LOG_INFO("session {} ignoring playback_finished for obsolete session current is {}", session_id, current_session_id_);
        return;
    }
    LOG_INFO("flow end 3 4 received playback finished from player for session {}", session_id);
    LOG_INFO("flow end 4 4 notifying spectrum to stop for session {}", session_id);
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
        LOG_WARN("session {} ignoring seek_finished for obsolete session current is {}", session_id, current_session_id_);
        return;
    }
    LOG_INFO("flow seek 5 10 received seek result from decoder for session {} {}ms", session_id, actual_seek_ms);

    if (actual_seek_ms < 0)
    {
        LOG_WARN("flow seek 6 10 seek failed for session {} resuming playback", session_id);
        is_seeking_ = false;
        pending_seek_ms_ = -1;
        if (is_playing_ && player_ != nullptr)
        {
            QMetaObject::invokeMethod(player_, "resume_feeding", Qt::QueuedConnection, Q_ARG(qint64, session_id));
        }
        emit seek_finished(false);
        return;
    }

    if (total_duration_ms_ > 0 && total_duration_ms_ - actual_seek_ms < 250)
    {
        LOG_INFO("session {} seek result is at the end transitioning to finished state", session_id);
        is_seeking_ = false;
        if (player_ != nullptr)
        {
            QMetaObject::invokeMethod(player_, "handle_seek", Qt::QueuedConnection, Q_ARG(qint64, session_id), Q_ARG(qint64, actual_seek_ms));
        }
        on_playback_finished(session_id);
        return;
    }

    LOG_INFO("flow seek 7 10 notifying player to handle seek for session {}", session_id);
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
    LOG_INFO("flow seek 8 10 received seek handled signal from player for session {}", session_id);

    if (player_ != nullptr)
    {
        QMetaObject::invokeMethod(player_, "resume_feeding", Qt::QueuedConnection, Q_ARG(qint64, session_id));
    }
    LOG_INFO("flow seek 9 10 notifying spectrum to reset for seek for session {}", session_id);

    if (spectrum_widget_ != nullptr)
    {
        QMetaObject::invokeMethod(
            spectrum_widget_, "reset_and_start", Qt::QueuedConnection, Q_ARG(qint64, session_id), Q_ARG(qint64, seek_result_ms_));
    }
    emit seek_finished(true);

    if (pending_seek_ms_ != -1)
    {
        LOG_INFO("session {} pending seek found to {}ms starting it now", session_id, pending_seek_ms_);
        qint64 new_seek_pos = pending_seek_ms_;
        pending_seek_ms_ = -1;
        seek(new_seek_pos);
        return;
    }

    is_seeking_ = false;
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
        LOG_DEBUG("player and player thread cleaned up");
    }
}
