#pragma once
#include <rigtorp/SPSCQueue.h>

template <typename T>
class RigtorpAdapter
{
public:
    explicit RigtorpAdapter(size_t capacity)
        : q(capacity) {}

    bool enqueue(T v)
    {
        return q.try_push(v);
    }

    bool dequeue(T &out)
    {
        T *ptr = q.front();
        if (!ptr)
            return false;

        out = *ptr;
        q.pop();
        return true;
    }

private:
    rigtorp::SPSCQueue<T> q;
};