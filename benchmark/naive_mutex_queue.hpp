#ifndef NAIVE_MUTEX_QUEUE_HPP
#define NAIVE_MUTEX_QUEUE_HPP

#include <queue>
#include <mutex>
#include <cstddef>

template <typename T>
class NaiveMutexQueue
{
public:
    using value_type = T;
    explicit NaiveMutexQueue(std::size_t capacity)
        : capacity_(capacity) {}

    bool enqueue(const T &v)
    {
        std::lock_guard<std::mutex> g(m_);
        if (q_.size() >= capacity_)
            return false;

        q_.push(v);
        return true;
    }

    bool dequeue(T &out)
    {
        std::lock_guard<std::mutex> g(m_);
        if (q_.empty())
            return false;

        out = q_.front();
        q_.pop();
        return true;
    }

private:
    std::queue<T> q_;
    std::mutex m_;
    const std::size_t capacity_;
};

#endif