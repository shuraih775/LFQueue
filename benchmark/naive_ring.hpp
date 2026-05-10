#ifndef NAIVE_RING_HPP
#define NAIVE_RING_HPP

#include <vector>
#include <atomic>
#include <cstddef>

template <typename T>
class NaiveRing
{
public:
    using value_type = T;
    explicit NaiveRing(size_t size)
        : size_(size),
          mask_(size - 1),
          buf_(size) {}

    bool enqueue(const T &v)
    {
        auto head = head_.load(std::memory_order_relaxed);
        auto tail = tail_.load(std::memory_order_acquire);

        if ((head - tail) == size_)
            return false;

        buf_[head & mask_] = v;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    bool dequeue(T &out)
    {
        auto tail = tail_.load(std::memory_order_relaxed);
        auto head = head_.load(std::memory_order_acquire);

        if (head == tail)
            return false;

        out = buf_[tail & mask_];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

private:
    size_t size_;
    size_t mask_;
    std::vector<T> buf_;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};

#endif