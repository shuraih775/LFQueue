#pragma once
#include <concurrentqueue.h>
#include <cstddef>

template <typename T>
class MoodyCamelBatchAdapter
{
public:
    using value_type = T;

    explicit MoodyCamelBatchAdapter(std::size_t capacity)
        : q(capacity),
          producer(q),
          consumer(q) {}

    bool enqueue_batch(const T *items, std::size_t n)
    {
        return q.enqueue_bulk(producer, items, n);
    }

    std::size_t dequeue_batch(T *out_items, std::size_t n)
    {
        return q.try_dequeue_bulk(consumer, out_items, n);
    }

private:
    moodycamel::ConcurrentQueue<T> q;
    moodycamel::ProducerToken producer;
    moodycamel::ConsumerToken consumer;
};