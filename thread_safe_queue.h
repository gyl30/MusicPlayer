#ifndef THREAD_SAFE_QUEUE_H
#define THREAD_SAFE_QUEUE_H

#include <mutex>
#include <cstdint>
#include <vector>
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
    void enqueue_front(const std::shared_ptr<audio_packet>& packet)
    {
        std::lock_guard<std::mutex> m(mutex_);
        queue_.insert(queue_.begin(), packet);
        bytes_size_ += packet->data.size();
        condition_.notify_one();
    }

    std::shared_ptr<audio_packet> dequeue()
    {
        std::unique_lock<std::mutex> mutex(mutex_);
        while (queue_.empty())
        {
            condition_.wait(mutex);
        }
        auto pkt = queue_.front();
        queue_.erase(queue_.begin());
        bytes_size_ -= pkt->data.size();
        return pkt;
    }

    std::shared_ptr<audio_packet> try_dequeue()
    {
        std::lock_guard<std::mutex> mutex(mutex_);
        if (queue_.empty())
        {
            return nullptr;
        }
        auto pkt = queue_.front();
        queue_.erase(queue_.begin());
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

    int size()
    {
        std::lock_guard<std::mutex> mutex(mutex_);
        return static_cast<int>(bytes_size_);
    }

   private:
    std::mutex mutex_;
    uint64_t bytes_size_ = 0;
    std::condition_variable condition_;
    std::vector<std::shared_ptr<audio_packet>> queue_;
};

#endif
