#pragma once
#include <dro/spsc-queue.hpp>

template <typename T>
class DrogalisAdapter
{
public:
    explicit DrogalisAdapter(size_t capacity)
        : q(capacity) {}

    bool enqueue(T v)
    {
        return q.try_push(v);
    }

    bool dequeue(T &out)
    {
        return q.try_pop(out);
    }

private:
    dro::SPSCQueue<T> q;
};