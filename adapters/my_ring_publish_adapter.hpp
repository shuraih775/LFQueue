#pragma once

#include <ring.hpp>
#include <cstddef>

template <typename T>
class LFPublishAdapter
{
public:
    using value_type = T;

    explicit LFPublishAdapter(
        size_t capacity,
        uint32_t publish_every)
        : q(capacity, publish_every)
    {
    }

    std::size_t enqueue_batch(
        const T *items,
        std::size_t n)
    {
        return q.enqueue_batch(
            items,
            n);
    }

    std::size_t dequeue_batch(
        T *out,
        std::size_t n)
    {
        return q.dequeue_batch(
            out,
            n);
    }

    void flush()
    {
        q.flush();
    }

private:
    lockfree::SPSCQueueBatch<T> q;
};