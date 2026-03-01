#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <cstring>
#include <algorithm>
#include <numeric>

#include "naive_mutex_queue.hpp"
#include "naive_ring.hpp"
#include "../include/ring.hpp"
#include "../adapters/boost_adapter.hpp"
#include "../adapters/drogalis_adapter.hpp"
#include "../adapters/moodycamel_latency_adapter.hpp"
#include "../adapters/moodycamel_throughput_adapter.hpp"
#include "../adapters/my_ring_latency_adapter.hpp"
#include "../adapters/my_ring_throughput_adapter.hpp"
#include "../adapters/rigtorp_adapter.hpp"

using Clock = std::chrono::high_resolution_clock;

// Precision timing for x86/Ryzen
#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
inline void cpu_relax() { __builtin_ia32_pause(); }
inline unsigned long long rdtsc() { return __rdtsc(); }
#else
inline void cpu_relax() {}
inline unsigned long long rdtsc() { return 0; } // Fallback
#endif

constexpr size_t RING_SIZE = 1 << 16;
constexpr size_t OPS = 50'000'000;
constexpr size_t LATENCY_SAMPLES = 1'000'000;

// --- Latency Benchmark (Ping-Pong) ---
template <typename Queue>
void run_latency_benchmark(const char *name)
{
    Queue q_ping(1024);
    Queue q_pong(1024);
    std::atomic<bool> start{false};
    std::vector<unsigned long long> latencies;
    latencies.reserve(LATENCY_SAMPLES);

    std::thread consumer([&]
                         {
        while (!start.load(std::memory_order_acquire)) cpu_relax();
        for (size_t i = 0; i < LATENCY_SAMPLES; ++i) {
            void* val;
            while (!q_ping.dequeue(val)) cpu_relax();
            while (!q_pong.enqueue(val)) cpu_relax();
        } });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    start.store(true, std::memory_order_release);

    for (size_t i = 0; i < LATENCY_SAMPLES; ++i)
    {
        unsigned long long t1 = rdtsc();
        q_ping.enqueue(reinterpret_cast<void *>(t1));

        void *t2_ptr;
        while (!q_pong.dequeue(t2_ptr))
            cpu_relax();
        unsigned long long t2 = rdtsc();

        latencies.push_back((t2 - t1) / 2); // One-way latency
    }

    consumer.join();

    std::sort(latencies.begin(), latencies.end());
    auto avg = std::accumulate(latencies.begin(), latencies.end(), 0ULL) / LATENCY_SAMPLES;

    std::cout << name << " Latency (Cycles):\n";
    std::cout << "  Avg: " << avg << " | P50: " << latencies[LATENCY_SAMPLES / 2]
              << " | P99: " << latencies[LATENCY_SAMPLES * 99 / 100] << "\n\n";
}

// --- Throughput Benchmark ---
template <typename Queue>
void run_spsc_benchmark(const char *name)
{
    Queue q(RING_SIZE);
    std::atomic<bool> start{false};
    std::atomic<bool> done{false};

    std::thread producer([&]
                         {
        while (!start.load(std::memory_order_acquire)) cpu_relax();
        for (size_t i = 0; i < OPS; ++i) {
            while (!q.enqueue(reinterpret_cast<void*>(i))) cpu_relax();
        }
        done.store(true, std::memory_order_release); });

    std::thread consumer([&]
                         {
        void* out;
        while (!start.load(std::memory_order_acquire)) cpu_relax();
        size_t count = 0;
        while (!done.load(std::memory_order_acquire) || count < OPS) {
            if (q.dequeue(out)) ++count;
            else cpu_relax();
        } });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto t0 = Clock::now();
    start.store(true, std::memory_order_release);

    producer.join();
    consumer.join();

    auto t1 = Clock::now();
    double seconds = std::chrono::duration<double>(t1 - t0).count();
    std::cout << name << " Throughput:\n";
    std::cout << "  Time: " << seconds << " s | " << (OPS / seconds / 1e6) << " M ops/sec\n\n";
}

template <typename Queue>
void edge_case_test(const char *name)
{
    Queue q(1024);
    void *out;
    if (q.dequeue(out))
        std::cout << name << " failed empty test\n";
    size_t pushed = 0;
    while (q.enqueue(reinterpret_cast<void *>(pushed)))
        ++pushed;
    size_t popped = 0;
    while (q.dequeue(out))
        ++popped;
    if (pushed != popped)
        std::cout << name << " failed count test\n";
}

int main()
{
    std::cout << "-- Edge Case Tests --\n";

    edge_case_test<NaiveRing<void *>>("NaiveRing");
    edge_case_test<LFQueueThroughputAdapter<void *>>("LFQueue");
    edge_case_test<DrogalisAdapter<void *>>("Drogalis");
    edge_case_test<BoostSPSCAdapter<void *>>("BoostSPSC");
    edge_case_test<MoodyCamelThroughputAdapter<void *>>("MoodyCamel");

    std::cout << "\n-- SPSC Throughput --\n";

    run_spsc_benchmark<NaiveMutexQueue<void *>>("MutexQueue");
    run_spsc_benchmark<NaiveRing<void *>>("NaiveRing");
    run_spsc_benchmark<LFQueueThroughputAdapter<void *>>("LFQueue");

    run_spsc_benchmark<RigtorpAdapter<void *>>("Rigtorp");
    run_spsc_benchmark<DrogalisAdapter<void *>>("Drogalis");
    run_spsc_benchmark<BoostSPSCAdapter<void *>>("BoostSPSC");
    run_spsc_benchmark<MoodyCamelThroughputAdapter<void *>>("MoodyCamel");

    std::cout << "\n-- SPSC Latency (One-Way) --\n";

    run_latency_benchmark<NaiveMutexQueue<void *>>("MutexQueue");
    run_latency_benchmark<NaiveRing<void *>>("NaiveRing");
    run_latency_benchmark<LFQueueLatencyAdapter<void *>>("LFQueue");

    run_latency_benchmark<RigtorpAdapter<void *>>("Rigtorp");
    run_latency_benchmark<DrogalisAdapter<void *>>("Drogalis");
    run_latency_benchmark<BoostSPSCAdapter<void *>>("BoostSPSC");

    // included but clearly not native SPSC, kept to later compare with mpmc implementation
    run_latency_benchmark<MoodyCamelLatencyAdapter<void *>>("MoodyCamel");

    return 0;
}