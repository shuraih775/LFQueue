#pragma once
#include <ring.hpp>

template <typename T>
class LFQueueThroughputAdapter
{
public:
    explicit LFQueueThroughputAdapter(size_t capacity)
        : q(capacity) {}

    bool enqueue(T v)
    {
        return q.enqueue(v); // no flush
    }

    bool dequeue(T &out)
    {
        return q.dequeue(out);
    }

private:
    lockfree::Ring<T> q;
};