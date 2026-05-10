#include <atomic>
#include <cstdint>
#include <thread>
#include <pthread.h>
#include <sched.h>

#include "../adapters/drogalis_adapter.hpp"

constexpr size_t N = 1 << 16;
constexpr size_t OPS = 10'000'000;

static inline void cpu_relax()
{
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#endif
}

static void pin_thread(int cpu)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);

    pthread_setaffinity_np(
        pthread_self(),
        sizeof(cpu_set_t),
        &cpuset);
}

__attribute__((noinline)) void producer(
    DrogalisAdapter<uint64_t> &q,
    std::atomic<bool> &start)
{
    pin_thread(0);

    while (!start.load(std::memory_order_acquire))
        cpu_relax();

    uint64_t x = 42;

    for (size_t i = 0; i < OPS; ++i)
    {
        while (!q.enqueue(x))
            cpu_relax();

        ++x;
    }
}

__attribute__((noinline)) void consumer(
    DrogalisAdapter<uint64_t> &q,
    std::atomic<bool> &start)
{
    pin_thread(2);

    while (!start.load(std::memory_order_acquire))
        cpu_relax();

    uint64_t x;

    for (size_t i = 0; i < OPS; ++i)
    {
        while (!q.dequeue(x))
            cpu_relax();
    }
}

int main()
{
    DrogalisAdapter<uint64_t> q(N);

    std::atomic<bool> start{false};

    std::thread prod(producer, std::ref(q), std::ref(start));
    std::thread cons(consumer, std::ref(q), std::ref(start));

    start.store(true, std::memory_order_release);

    prod.join();
    cons.join();

    return 0;
}
