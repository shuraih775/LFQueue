#pragma once
#include <concurrentqueue.h>

template <typename T>
class MoodyCamelLatencyAdapter
{
public:
    explicit MoodyCamelLatencyAdapter(size_t capacity)
        : q(capacity),
          producer(q),
          consumer(q) {}

    bool enqueue(T v)
    {
        bool ok = q.try_enqueue(v);
        return ok;
    }

    bool dequeue(T &out)
    {
        return q.try_dequeue(out);
    }

private:
    moodycamel::ConcurrentQueue<T> q;
    moodycamel::ProducerToken producer;
    moodycamel::ConsumerToken consumer;
};