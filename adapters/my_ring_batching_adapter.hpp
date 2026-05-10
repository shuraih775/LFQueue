#pragma once

#include <ring.hpp>
#include <cstddef>

template <
    typename T,
    typename QueueType =
        lockfree::SPSCQueueBatch<T>>
class LFQueueBatchAdapter
{
public:
    using value_type = T;

    explicit LFQueueBatchAdapter(
        std::size_t capacity,
        uint32_t publish_batch = 32)
        : q(capacity, publish_batch)
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
        T *out_items,
        std::size_t n)
    {
        return q.dequeue_batch(
            out_items,
            n);
    }

    bool enqueue(const T &v)
    {
        return q.enqueue_batch(
                   &v,
                   1) == 1;
    }

    bool dequeue(T &out)
    {
        return q.dequeue_batch(
                   &out,
                   1) == 1;
    }

    void flush()
    {
        q.flush();
    }

private:
    QueueType q;
};