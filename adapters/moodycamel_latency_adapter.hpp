#pragma once
#include <blockingconcurrentqueue.h>

template <typename T>
class MoodyCamelLatencyAdapter
{
public:
    using value_type = T;
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
    moodycamel::BlockingConcurrentQueue<T> q;
    moodycamel::ProducerToken producer;
    moodycamel::ConsumerToken consumer;
};