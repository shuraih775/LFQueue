#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <type_traits>
#include <iostream>
#include <numeric>
#include <pthread.h>
#include <sched.h>
#include <thread>
#include <vector>
#include <cstring>
#include "naive_mutex_queue.hpp"
#include "naive_ring.hpp"

#include "../include/ring.hpp"

#include "../adapters/boost_adapter.hpp"
#include "../adapters/drogalis_adapter.hpp"
#include "../adapters/moodycamel_latency_adapter.hpp"
#include "../adapters/moodycamel_throughput_adapter.hpp"
#include "../adapters/moodycamel_batching_adapter.hpp"
#include "../adapters/my_ring_adapter.hpp"
#include "../adapters/my_ring_batching_adapter.hpp"
#include "../adapters/my_ring_publish_adapter.hpp"
#include "../adapters/rigtorp_adapter.hpp"

using Clock = std::chrono::steady_clock;

#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>

inline void cpu_relax() noexcept
{
    __builtin_ia32_pause();
}

inline uint64_t rdtsc() noexcept
{
    unsigned aux;
    _mm_lfence();
    return __rdtscp(&aux);
}
#else
inline void cpu_relax() noexcept {}
inline uint64_t rdtsc() noexcept { return 0; }
#endif

constexpr size_t OPS = 10'000'000;
constexpr size_t LATENCY_SAMPLES = 1'000'000;
constexpr size_t QUEUE_SIZE = 1 << 16;

constexpr size_t BATCH_SIZE = 32;
constexpr size_t BATCH_OPS =
    OPS / BATCH_SIZE;

template <typename T>
struct TimedPayload
{
    uint64_t tsc;
    T payload;
};
template <typename T>
T make_value(uint64_t v)
{
    T out{};

    std::memcpy(
        &out,
        &v,
        std::min(
            sizeof(T),
            sizeof(v)));

    return out;
}

inline uint64_t extract_timestamp(
    const uint64_t &v)
{
    return v;
}

template <typename T>
inline uint64_t extract_timestamp(
    const TimedPayload<T> &v)
{
    return v.tsc;
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

template <typename Queue>
void edge_case_test(const char *name)
{
    Queue q(1024);

    typename Queue::value_type out;

    if (q.dequeue(out))
    {
        std::cout << name << " failed empty test\n";
    }

    size_t pushed = 0;

    while (q.enqueue(reinterpret_cast<void *>(pushed)))
    {
        ++pushed;
    }

    size_t popped = 0;

    while (q.dequeue(out))
    {
        ++popped;
    }

    if (pushed != popped)
    {
        std::cout << name << " failed count test\n";
    }
}

template <typename Queue>
void run_hotpath_throughput_benchmark(
    const char *name,
    int producer_cpu,
    int consumer_cpu)
{
    constexpr int RUNS = 10;

    constexpr size_t WARMUP_OPS = 100000;
    constexpr size_t PAYLOAD_POOL = 4096;

    std::vector<double> throughputs;
    throughputs.reserve(RUNS);

    for (int run = 0; run < RUNS; ++run)
    {
        Queue q(QUEUE_SIZE);

        using T = typename Queue::value_type;

        std::vector<T> payloads;
        payloads.resize(PAYLOAD_POOL);
        uint64_t pv = 42;
        for (size_t i = 0; i < PAYLOAD_POOL; ++i)
        {
            payloads[i] = make_value<T>(pv++);
        }

        std::atomic<bool> start{false};

        auto producer = [&]()
        {
            pin_thread(producer_cpu);

            while (!start.load(std::memory_order_acquire))
            {
                cpu_relax();
            }

            for (size_t i = 0; i < OPS; ++i)
            {
                while (!q.enqueue(payloads[i & (PAYLOAD_POOL - 1)]))
                {
                    cpu_relax();
                }
            }
        };

        auto consumer = [&]()
        {
            pin_thread(consumer_cpu);

            while (!start.load(std::memory_order_acquire))
            {
                cpu_relax();
            }

            typename Queue::value_type out;

            for (size_t i = 0; i < OPS; ++i)
            {
                while (!q.dequeue(out))
                {
                    cpu_relax();
                }
            }
        };

        std::thread prod(producer);
        std::thread cons(consumer);

        std::this_thread::sleep_for(
            std::chrono::milliseconds(1000));

        auto t0 = Clock::now();

        start.store(
            true,
            std::memory_order_release);

        prod.join();
        cons.join();

        auto t1 = Clock::now();

        double seconds =
            std::chrono::duration<double>(t1 - t0).count();

        double throughput =
            OPS / seconds / 1e6;

        throughputs.push_back(throughput);
    }

    std::sort(
        throughputs.begin(),
        throughputs.end());

    double mean =
        std::accumulate(
            throughputs.begin(),
            throughputs.end(),
            0.0) /
        RUNS;

    double median =
        throughputs[RUNS / 2];

    std::cout << name << " Throughput:\n";

    std::cout
        << "  Mean: " << mean
        << " M ops/sec"
        << " | Median: " << median
        << " | Min: " << throughputs.front()
        << " | Max: " << throughputs.back()
        << "\n\n";
}

template <typename Queue>
void run_hotpath_latency_benchmark(
    const char *name,
    int producer_cpu,
    int consumer_cpu)
{
    constexpr int RUNS = 10;

    constexpr size_t PAYLOAD_POOL = 4096;

    std::vector<uint64_t> avg_runs;
    std::vector<uint64_t> p50_runs;
    std::vector<uint64_t> p99_runs;

    avg_runs.reserve(RUNS);
    p50_runs.reserve(RUNS);
    p99_runs.reserve(RUNS);

    for (int run = 0; run < RUNS; ++run)
    {
        Queue ping(1024);
        Queue pong(1024);

        using T = typename Queue::value_type;

        std::vector<T> prebuilt;
        prebuilt.resize(PAYLOAD_POOL);
        for (size_t i = 0; i < PAYLOAD_POOL; ++i)
        {
            prebuilt[i].payload = make_value<decltype(prebuilt[i].payload)>(i);
            prebuilt[i].tsc = 0;
        }

        std::atomic<bool> start{false};

        std::vector<uint64_t> latencies;
        latencies.reserve(LATENCY_SAMPLES);

        std::thread consumer([&]
                             {
            pin_thread(consumer_cpu);

            while (!start.load(std::memory_order_acquire))
            {
                cpu_relax();
            }

            for (size_t i = 0; i < LATENCY_SAMPLES; ++i)
            {
                typename Queue::value_type value;

                while (!ping.dequeue(value))
                {
                    cpu_relax();
                }

                while (!pong.enqueue(value))
                {
                    cpu_relax();
                }
            } });

        pin_thread(producer_cpu);

        std::this_thread::sleep_for(
            std::chrono::milliseconds(1000));

        start.store(true, std::memory_order_release);

        for (size_t i = 0; i < LATENCY_SAMPLES; ++i)
        {
            uint64_t t1 = rdtsc();

            size_t idx = i & (PAYLOAD_POOL - 1);
            prebuilt[idx].tsc = t1;

            while (!ping.enqueue(prebuilt[idx]))
            {
                cpu_relax();
            }

            typename Queue::value_type out;

            while (!pong.dequeue(out))
            {
                cpu_relax();
            }

            uint64_t t2 = rdtsc();

            latencies.push_back((t2 - out.tsc) / 2);
        }

        consumer.join();

        std::sort(
            latencies.begin(),
            latencies.end());

        uint64_t avg =
            std::accumulate(
                latencies.begin(),
                latencies.end(),
                uint64_t(0)) /
            LATENCY_SAMPLES;

        uint64_t p50 =
            latencies[LATENCY_SAMPLES / 2];

        uint64_t p99 =
            latencies[(LATENCY_SAMPLES * 99) / 100];

        avg_runs.push_back(avg);
        p50_runs.push_back(p50);
        p99_runs.push_back(p99);
    }

    auto calc_mean = [](const auto &v)
    {
        return std::accumulate(
                   v.begin(),
                   v.end(),
                   uint64_t(0)) /
               v.size();
    };

    std::sort(avg_runs.begin(), avg_runs.end());
    std::sort(p50_runs.begin(), p50_runs.end());
    std::sort(p99_runs.begin(), p99_runs.end());

    std::cout << name << " Latency (Cycles):\n";

    std::cout
        << "  Avg Mean: " << calc_mean(avg_runs)
        << " | Avg Median: " << avg_runs[RUNS / 2]
        << "\n";

    std::cout
        << "  P50 Mean: " << calc_mean(p50_runs)
        << " | P50 Median: " << p50_runs[RUNS / 2]
        << "\n";

    std::cout
        << "  P99 Mean: " << calc_mean(p99_runs)
        << " | P99 Median: " << p99_runs[RUNS / 2]
        << "\n\n";
}

struct Payload2
{
    uint16_t value;
};

struct Payload4
{
    uint32_t value;
};

struct Payload8
{
    uint64_t value;
};

struct Payload16
{
    uint64_t a;
    uint64_t b;
};
struct alignas(32) Payload32
{
    uint64_t a, b, c, d;
};
struct alignas(64) Payload64
{
    uint64_t a, b, c, d, e, f, g, h;
};

template <typename T>
void run_payload_suite(
    const char *label,
    int producer_cpu,
    int consumer_cpu)
{
    std::cout
        << "\n\n";

    std::cout
        << label
        << " ("
        << sizeof(T)
        << " bytes)\n";

    std::cout
        << "\n\n";

    // Verify trivial copyability and print layout diagnostics
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(std::is_trivially_copyable_v<TimedPayload<T>>);

    std::cout << "Layout: sizeof(T)=" << sizeof(T)
              << " alignof(T)=" << alignof(T)
              << " sizeof(Timed)=" << sizeof(TimedPayload<T>)
              << " alignof(Timed)=" << alignof(TimedPayload<T>) << "\n";

    std::cout << " Throughput \n";

    run_hotpath_throughput_benchmark<
        LFQueueAdapter<T>>(
        "LFQueue",
        producer_cpu,
        consumer_cpu);

    run_hotpath_throughput_benchmark<
        RigtorpAdapter<T>>(
        "Rigtorp",
        producer_cpu,
        consumer_cpu);

    run_hotpath_throughput_benchmark<
        DrogalisAdapter<T>>(
        "Drogalis",
        producer_cpu,
        consumer_cpu);

    run_hotpath_throughput_benchmark<
        BoostSPSCAdapter<T>>(
        "BoostSPSC",
        producer_cpu,
        consumer_cpu);

    std::cout << "\n Latency \n";

    run_hotpath_latency_benchmark<
        LFQueueAdapter<
            TimedPayload<T>>>(
        "LFQueue",
        producer_cpu,
        consumer_cpu);

    run_hotpath_latency_benchmark<
        RigtorpAdapter<
            TimedPayload<T>>>(
        "Rigtorp",
        producer_cpu,
        consumer_cpu);

    run_hotpath_latency_benchmark<
        DrogalisAdapter<
            TimedPayload<T>>>(
        "Drogalis",
        producer_cpu,
        consumer_cpu);

    run_hotpath_latency_benchmark<
        BoostSPSCAdapter<
            TimedPayload<T>>>(
        "BoostSPSC",
        producer_cpu,
        consumer_cpu);
}

template <typename Queue>
void run_batch_throughput_benchmark(
    const char *name,
    int producer_cpu,
    int consumer_cpu)
{
    constexpr int RUNS = 10;

    constexpr size_t PAYLOAD_POOL = 4096;

    std::vector<double> throughputs;
    throughputs.reserve(RUNS);

    for (int run = 0; run < RUNS; ++run)
    {
        Queue q(QUEUE_SIZE);

        using T = typename Queue::value_type;

        // Pre-generate small cyclic payload pool
        std::vector<T> payloads;
        payloads.resize(PAYLOAD_POOL);
        uint64_t counter_val = 0;
        for (size_t i = 0; i < PAYLOAD_POOL; ++i)
        {
            payloads[i] = make_value<T>(counter_val++);
        }

        std::atomic<bool> start{false};

        auto producer = [&]()
        {
            pin_thread(producer_cpu);

            while (!start.load(std::memory_order_acquire))
            {
                cpu_relax();
            }

            for (size_t i = 0; i < BATCH_OPS; ++i)
            {
                size_t base = (i * BATCH_SIZE) & (PAYLOAD_POOL - 1);
                while (q.enqueue_batch(&payloads[base], BATCH_SIZE) != BATCH_SIZE)
                {
                    cpu_relax();
                }
            }
        };

        auto consumer = [&]()
        {
            pin_thread(consumer_cpu);

            alignas(64) T out[BATCH_SIZE];

            while (!start.load(std::memory_order_acquire))
            {
                cpu_relax();
            }

            for (size_t i = 0; i < BATCH_OPS; ++i)
            {
                while (q.dequeue_batch(out, BATCH_SIZE) != BATCH_SIZE)
                {
                    cpu_relax();
                }
            }
        };

        std::thread prod(producer);
        std::thread cons(consumer);

        std::this_thread::sleep_for(
            std::chrono::milliseconds(1000));

        auto t0 = Clock::now();

        start.store(
            true,
            std::memory_order_release);

        prod.join();
        cons.join();

        auto t1 = Clock::now();

        double seconds =
            std::chrono::duration<double>(
                t1 - t0)
                .count();

        double throughput =
            OPS / seconds / 1e6;

        throughputs.push_back(throughput);
    }

    std::sort(
        throughputs.begin(),
        throughputs.end());

    double mean =
        std::accumulate(
            throughputs.begin(),
            throughputs.end(),
            0.0) /
        RUNS;

    std::cout
        << name
        << " Batch Throughput:\n";

    std::cout
        << "  Mean: " << mean
        << " M ops/sec"
        << " | Median: "
        << throughputs[RUNS / 2]
        << " | Min: "
        << throughputs.front()
        << " | Max: "
        << throughputs.back()
        << "\n\n";
}

template <typename Queue>
void run_batch_latency_benchmark(
    const char *name,
    int producer_cpu,
    int consumer_cpu)
{
    constexpr int RUNS = 10;

    constexpr size_t PAYLOAD_POOL = 4096;

    using T = typename Queue::value_type;

    std::vector<uint64_t> avg_runs;
    std::vector<uint64_t> p50_runs;
    std::vector<uint64_t> p99_runs;

    avg_runs.reserve(RUNS);
    p50_runs.reserve(RUNS);
    p99_runs.reserve(RUNS);

    for (int run = 0; run < RUNS; ++run)
    {
        Queue ping(QUEUE_SIZE);
        Queue pong(QUEUE_SIZE);

        // Pre-generate small cyclic payload pool
        std::vector<T> prebuilt;
        prebuilt.resize(PAYLOAD_POOL);
        for (size_t i = 0; i < PAYLOAD_POOL; ++i)
        {
            prebuilt[i].payload = make_value<decltype(prebuilt[i].payload)>(i);
            prebuilt[i].tsc = 0;
        }

        std::atomic<bool> start{false};

        alignas(64) T out[BATCH_SIZE];

        std::vector<uint64_t> latencies;

        latencies.reserve(LATENCY_SAMPLES / BATCH_SIZE);

        std::thread consumer([&]
                             {
            pin_thread(consumer_cpu);

            while (!start.load(
                std::memory_order_acquire))
            {
                cpu_relax();
            }

            for (size_t i = 0; i < LATENCY_SAMPLES / BATCH_SIZE; ++i)
            {
                while (ping.dequeue_batch(out, BATCH_SIZE) != BATCH_SIZE)
                {
                    cpu_relax();
                }

                while (pong.enqueue_batch(out, BATCH_SIZE) != BATCH_SIZE)
                {
                    cpu_relax();
                }
            } });

        pin_thread(producer_cpu);

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        start.store(true, std::memory_order_release);

        for (size_t i = 0; i < LATENCY_SAMPLES / BATCH_SIZE; ++i)
        {
            uint64_t t1 = rdtsc();
            size_t base = (i * BATCH_SIZE) & (PAYLOAD_POOL - 1);
            for (size_t j = 0; j < BATCH_SIZE; ++j)
            {
                prebuilt[(base + j) & (PAYLOAD_POOL - 1)].tsc = t1;
            }

            while (ping.enqueue_batch(&prebuilt[base], BATCH_SIZE) != BATCH_SIZE)
            {
                cpu_relax();
            }

            while (pong.dequeue_batch(out, BATCH_SIZE) != BATCH_SIZE)
            {
                cpu_relax();
            }

            uint64_t t2 = rdtsc();

            latencies.push_back((t2 - extract_timestamp(out[0])) / 2);
        }

        consumer.join();

        std::sort(
            latencies.begin(),
            latencies.end());

        uint64_t avg =
            std::accumulate(
                latencies.begin(),
                latencies.end(),
                uint64_t(0)) /
            latencies.size();

        uint64_t p50 =
            latencies[latencies.size() / 2];

        uint64_t p99 =
            latencies[(latencies.size() * 99) / 100];

        avg_runs.push_back(avg);
        p50_runs.push_back(p50);
        p99_runs.push_back(p99);
    }

    auto calc_mean = [](const auto &v)
    {
        return std::accumulate(
                   v.begin(),
                   v.end(),
                   uint64_t(0)) /
               v.size();
    };

    std::sort(avg_runs.begin(), avg_runs.end());
    std::sort(p50_runs.begin(), p50_runs.end());
    std::sort(p99_runs.begin(), p99_runs.end());

    std::cout
        << name
        << " Batch Latency (Cycles):\n";

    std::cout
        << "  Avg Mean: "
        << calc_mean(avg_runs)
        << " | Avg Median: "
        << avg_runs[RUNS / 2]
        << "\n";

    std::cout
        << "  P50 Mean: "
        << calc_mean(p50_runs)
        << " | P50 Median: "
        << p50_runs[RUNS / 2]
        << "\n";

    std::cout
        << "  P99 Mean: "
        << calc_mean(p99_runs)
        << " | P99 Median: "
        << p99_runs[RUNS / 2]
        << "\n\n";
}

template <typename Queue>
void run_burst_sweep(
    const char *name,
    int producer_cpu,
    int consumer_cpu)
{
    constexpr size_t BURSTS[] =
        {
            1, 2, 4, 8,
            16, 32, 64};

    using T = typename Queue::value_type;

    std::cout
        << "\n Burst Sweep: "
        << name
        << " \n\n";

    for (size_t burst : BURSTS)
    {
        Queue q(QUEUE_SIZE);

        std::atomic<bool> start{false};

        alignas(64)
            T batch[64];

        for (size_t i = 0; i < 64; ++i)
        {
            batch[i] =
                make_value<T>(i);
        }

        auto producer = [&]()
        {
            pin_thread(producer_cpu);

            while (!start.load(
                std::memory_order_acquire))
            {
                cpu_relax();
            }

            size_t produced = 0;

            while (produced < OPS)
            {
                while (
                    q.enqueue_batch(
                        batch,
                        burst) != burst)
                {
                    cpu_relax();
                }

                produced += burst;
            }
        };

        auto consumer = [&]()
        {
            pin_thread(consumer_cpu);

            alignas(64)
                T out[64];

            while (!start.load(
                std::memory_order_acquire))
            {
                cpu_relax();
            }

            size_t consumed = 0;

            while (consumed < OPS)
            {
                while (
                    q.dequeue_batch(
                        out,
                        burst) != burst)
                {
                    cpu_relax();
                }

                consumed += burst;
            }
        };

        std::thread prod(producer);
        std::thread cons(consumer);

        std::this_thread::sleep_for(
            std::chrono::milliseconds(1000));

        auto t0 = Clock::now();

        start.store(
            true,
            std::memory_order_release);

        prod.join();
        cons.join();

        auto t1 = Clock::now();

        double sec =
            std::chrono::duration<double>(
                t1 - t0)
                .count();

        double throughput =
            OPS / sec / 1e6;

        std::cout
            << "Burst "
            << burst
            << ": "
            << throughput
            << " M ops/sec\n";
    }

    std::cout << '\n';
}

template <typename T>
void run_publish_sweep(
    int producer_cpu,
    int consumer_cpu)
{
    constexpr uint32_t PUBS[] =
        {
            1,
            2,
            4,
            8,
            16,
            32,
            64};

    std::cout
        << "\n Publication Sweep \n\n";

    for (uint32_t pub : PUBS)
    {
        LFPublishAdapter<T> q(QUEUE_SIZE, pub);

        std::atomic<bool> start{false};

        alignas(64) T batch[BATCH_SIZE];
        alignas(64) T out[BATCH_SIZE];

        // Pre-fill a batch once to avoid construction in hot loop
        for (size_t i = 0; i < BATCH_SIZE; ++i)
        {
            batch[i] = make_value<T>(i);
        }

        auto producer = [&]()
        {
            pin_thread(producer_cpu);

            while (!start.load(std::memory_order_acquire))
            {
                cpu_relax();
            }

            size_t produced = 0;
            while (produced < OPS)
            {
                while (q.enqueue_batch(batch, BATCH_SIZE) != BATCH_SIZE)
                {
                    cpu_relax();
                }

                produced += BATCH_SIZE;
            }
        };

        auto consumer = [&]()
        {
            pin_thread(consumer_cpu);

            while (!start.load(std::memory_order_acquire))
            {
                cpu_relax();
            }

            size_t consumed = 0;
            while (consumed < OPS)
            {
                while (q.dequeue_batch(out, BATCH_SIZE) != BATCH_SIZE)
                {
                    cpu_relax();
                }

                consumed += BATCH_SIZE;
            }
        };

        std::thread prod(producer);
        std::thread cons(consumer);

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        auto t0 = Clock::now();

        start.store(true, std::memory_order_release);

        prod.join();
        cons.join();

        auto t1 = Clock::now();

        double sec = std::chrono::duration<double>(t1 - t0).count();

        double throughput = OPS / sec / 1e6;

        std::cout << "Publish " << pub << ": " << throughput << " M ops/sec\n";
    }

    std::cout << '\n';
}
template <typename Queue>
void run_occupancy_benchmark(
    const char *name,
    int producer_cpu,
    int consumer_cpu)
{
    constexpr double OCCS[] =
        {
            0.25,
            0.50,
            0.75,
            0.95};

    using T = typename Queue::value_type;

    std::cout
        << "\n Occupancy Sweep: "
        << name
        << " \n\n";

    for (double occ : OCCS)
    {
        Queue q(QUEUE_SIZE);

        size_t prefill = static_cast<size_t>(QUEUE_SIZE * occ);

        std::vector<T> prefill_payloads;
        prefill_payloads.resize(prefill);
        for (size_t i = 0; i < prefill; ++i)
        {
            prefill_payloads[i] = make_value<T>(i);
        }

        for (size_t i = 0; i < prefill; ++i)
        {
            while (!q.enqueue(prefill_payloads[i]))
            {
                cpu_relax();
            }
        }

        std::atomic<bool> start{false};

        auto producer = [&]()
        {
            pin_thread(producer_cpu);

            T value =
                make_value<T>(42);

            while (!start.load(
                std::memory_order_acquire))
            {
                cpu_relax();
            }

            for (size_t i = 0; i < OPS; ++i)
            {
                while (!q.enqueue(value))
                {
                    cpu_relax();
                }
            }
        };

        auto consumer = [&]()
        {
            pin_thread(consumer_cpu);

            T out;

            while (!start.load(
                std::memory_order_acquire))
            {
                cpu_relax();
            }

            for (size_t i = 0; i < OPS; ++i)
            {
                while (!q.dequeue(out))
                {
                    cpu_relax();
                }
            }
        };

        std::thread prod(producer);
        std::thread cons(consumer);

        std::this_thread::sleep_for(
            std::chrono::milliseconds(1000));

        auto t0 = Clock::now();

        start.store(
            true,
            std::memory_order_release);

        prod.join();
        cons.join();

        auto t1 = Clock::now();

        double sec =
            std::chrono::duration<double>(
                t1 - t0)
                .count();

        double throughput =
            OPS / sec / 1e6;

        std::cout
            << "Occupancy "
            << occ * 100
            << "%: "
            << throughput
            << " M ops/sec\n";
    }

    std::cout << '\n';
}

int main(int argc, char **argv)
{
    int producer_cpu = 0;
    int consumer_cpu = 2;

    if (argc == 3)
    {
        producer_cpu = std::stoi(argv[1]);
        consumer_cpu = std::stoi(argv[2]);
    }

    std::cout << "Compiler: ";
#if defined(__clang__)
    std::cout << "Clang " << __clang_version__;
#elif defined(__GNUC__)
    std::cout << "GCC " << __VERSION__;
#else
    std::cout << "Unknown";
#endif
    std::cout << " | AVX2: ";
#if defined(__AVX2__)
    std::cout << "enabled";
#else
    std::cout << "disabled";
#endif
    std::cout << "\n";

    std::cout << "Payload alignments: Payload32=" << alignof(Payload32)
              << " Payload64=" << alignof(Payload64) << "\n";

    std::cout << " Edge Case Tests \n";

    edge_case_test<NaiveRing<void *>>("NaiveRing");

    edge_case_test<
        LFQueueAdapter<void *>>(
        "LFQueue");

    edge_case_test<
        RigtorpAdapter<void *>>(
        "Rigtorp");

    edge_case_test<
        DrogalisAdapter<void *>>(
        "Drogalis");

    edge_case_test<
        BoostSPSCAdapter<void *>>(
        "BoostSPSC");

    edge_case_test<
        MoodyCamelThroughputAdapter<void *>>(
        "MoodyCamel");

    std::cout << "\n POINTER \n";

    run_hotpath_throughput_benchmark<
        LFQueueAdapter<void *>>(
        "LFQueue",
        producer_cpu,
        consumer_cpu);

    run_hotpath_throughput_benchmark<
        RigtorpAdapter<void *>>(
        "Rigtorp",
        producer_cpu,
        consumer_cpu);

    run_hotpath_throughput_benchmark<
        DrogalisAdapter<void *>>(
        "Drogalis",
        producer_cpu,
        consumer_cpu);

    run_hotpath_throughput_benchmark<
        BoostSPSCAdapter<void *>>(
        "BoostSPSC",
        producer_cpu,
        consumer_cpu);

    std::cout << "\n  Payload Benchmark \n";

    run_payload_suite<
        Payload2>(
        "Payload2",
        producer_cpu,
        consumer_cpu);

    run_payload_suite<
        Payload4>(
        "Payload4",
        producer_cpu,
        consumer_cpu);

    run_payload_suite<
        Payload8>(
        "Payload8",
        producer_cpu,
        consumer_cpu);

    run_payload_suite<
        Payload16>(
        "Payload16",
        producer_cpu,
        consumer_cpu);

    run_payload_suite<
        Payload32>(
        "Payload32",
        producer_cpu,
        consumer_cpu);

    run_payload_suite<
        Payload64>(
        "Payload64",
        producer_cpu,
        consumer_cpu);

    std::cout
        << "\n BATCH \n";

    run_batch_throughput_benchmark<
        LFQueueBatchAdapter<
            TimedPayload<Payload8>>>(
        "LFQueueBatch",
        producer_cpu,
        consumer_cpu);

    run_batch_latency_benchmark<
        LFQueueBatchAdapter<
            TimedPayload<Payload8>>>(
        "LFQueueBatch",
        producer_cpu,
        consumer_cpu);

    run_burst_sweep<
        LFQueueBatchAdapter<
            TimedPayload<Payload8>>>(
        "LFQueue",
        producer_cpu,
        consumer_cpu);

    run_occupancy_benchmark<
        LFQueueAdapter<Payload8>>(
        "LFQueue",
        producer_cpu,
        consumer_cpu);

    run_publish_sweep<Payload8>(
        producer_cpu,
        consumer_cpu);

    return 0;
}