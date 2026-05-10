#pragma once
#include <ring.hpp>

template <typename T>
class LFQueueAdapter
{
public:
    using value_type = T;
    explicit LFQueueAdapter(size_t capacity)
        : q(capacity) {}

    bool enqueue(T v)
    {
        return q.enqueue(v);
    }

    bool dequeue(T &out)
    {
        return q.dequeue(out);
    }

private:
    lockfree::SPSCQueue<T> q;
};