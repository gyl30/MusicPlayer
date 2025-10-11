#ifndef THREAD_SAFE_QUEUE_H
#define THREAD_SAFE_QUEUE_H

#include <mutex>
#include <cstdint>
#include <deque>
#include <memory>
#include <condition_variable>
#include "audio_packet.h"

class safe_queue
{
   public:
    safe_queue() = default;

    void enqueue(const std::shared_ptr<audio_packet>& packet)
    {
        std::lock_guard<std::mutex> m(mutex_);
        queue_.push_back(packet);
        bytes_size_ += packet->data.size();
        condition_.notify_one();
    }

    std::shared_ptr<audio_packet> try_dequeue()
    {
        std::lock_guard<std::mutex> mutex(mutex_);
        if (queue_.empty())
        {
            return nullptr;
        }
        auto pkt = queue_.front();
        queue_.pop_front();
        bytes_size_ -= pkt->data.size();
        return pkt;
    }

    void clear()
    {
        std::lock_guard<std::mutex> mutex(mutex_);
        queue_.clear();
        bytes_size_ = 0;
    }

    bool is_empty()
    {
        std::lock_guard<std::mutex> mutex(mutex_);
        return queue_.empty();
    }

    size_t size_in_bytes()
    {
        std::lock_guard<std::mutex> mutex(mutex_);
        return bytes_size_;
    }

   private:
    std::mutex mutex_;
    qint64 bytes_size_ = 0;
    std::condition_variable condition_;
    std::deque<std::shared_ptr<audio_packet>> queue_;
};

#endif
