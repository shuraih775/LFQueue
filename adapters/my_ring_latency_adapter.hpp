#pragma once
#include <ring.hpp>

template <typename T>
class LFQueueLatencyAdapter
{
public:
    explicit LFQueueLatencyAdapter(size_t capacity)
        : q(capacity) {}

    bool enqueue(T v)
    {
        bool ok = q.enqueue(v);
        if (ok)
            q.flush();
        return ok;
    }

    bool dequeue(T &out)
    {
        return q.dequeue(out);
    }

private:
    lockfree::Ring<T> q;
};