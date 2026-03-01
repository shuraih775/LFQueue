#pragma once
#include <boost/lockfree/spsc_queue.hpp>

template <typename T>
class BoostSPSCAdapter
{
public:
    explicit BoostSPSCAdapter(size_t capacity)
        : q(capacity) {}

    bool enqueue(T v)
    {
        return q.push(v);
    }

    bool dequeue(T &out)
    {
        return q.pop(out);
    }

private:
    boost::lockfree::spsc_queue<T> q;
};