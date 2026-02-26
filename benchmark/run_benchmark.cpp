#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <cstring>

#include "naive_mutex_queue.hpp"
#include "naive_ring.hpp"
#include "../include/ring.hpp"

using Clock = std::chrono::high_resolution_clock;

#if defined(__x86_64__) || defined(__i386__)
inline void cpu_relax() { __builtin_ia32_pause(); }
#else
inline void cpu_relax() {}
#endif

constexpr size_t RING_SIZE = 1 << 16;
constexpr size_t OPS = 50'000'000;
constexpr size_t WARMUP = 1'000'000;

template <typename Queue>
void run_spsc_benchmark(const char *name)
{
    Queue q(RING_SIZE);

    std::atomic<bool> start{false};
    std::atomic<bool> done{false};

    // ---------------- PRODUCER ----------------
    std::thread producer([&]
                         {
        while (!start.load(std::memory_order_acquire)) cpu_relax();

        for (size_t i = 0; i < OPS; ++i) {
            while (!q.enqueue(reinterpret_cast<void*>(i))) {
                cpu_relax();
            }
        }

        done.store(true, std::memory_order_release); });

    // ---------------- CONSUMER ----------------
    std::thread consumer([&]
                         {
        void* out;

        while (!start.load(std::memory_order_acquire)) cpu_relax();

        size_t count = 0;
        while (!done.load(std::memory_order_acquire) || count < OPS) {
            if (q.dequeue(out)) {
                ++count;
            } else {
                cpu_relax();
            }
        } });

    // ---------------- WARMUP ----------------
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto t0 = Clock::now();
    start.store(true, std::memory_order_release);

    producer.join();
    consumer.join();

    auto t1 = Clock::now();

    double seconds =
        std::chrono::duration<double>(t1 - t0).count();

    double mops = OPS / seconds / 1e6;

    std::cout << name << "\n";
    std::cout << "  Time: " << seconds << " s\n";
    std::cout << "  Throughput: " << mops << " M ops/sec\n\n";
}

// Edge Case Tests
template <typename Queue>
void edge_case_test(const char *name)
{
    Queue q(1024);

    void *out;

    // empty dequeue
    if (q.dequeue(out))
        std::cout << name << " failed empty test\n";

    // fill completely
    size_t pushed = 0;
    for (;;)
    {
        if (!q.enqueue(reinterpret_cast<void *>(pushed)))
            break;
        ++pushed;
    }

    size_t popped = 0;
    while (q.dequeue(out))
        ++popped;

    if (pushed != popped)
        std::cout << name << " failed count test\n";
}

int main()
{
    std::cout << "-- Edge Case Tests --\n";
    edge_case_test<NaiveMutexQueue<void *>>("MutexQueue");
    edge_case_test<NaiveRing<void *>>("NaiveRing");
    edge_case_test<lockfree::Ring<void *>>("OptimizedRing");

    std::cout << "\n-- SPSC Throughput --\n";

    run_spsc_benchmark<NaiveMutexQueue<void *>>("MutexQueue");
    run_spsc_benchmark<NaiveRing<void *>>("NaiveRing");
    run_spsc_benchmark<lockfree::Ring<void *>>("OptimizedRing");

    return 0;
}