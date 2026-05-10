## Design & Inspiration

This project is inspired by the DPDK `rte_ring` architecture and focuses on building a high-performance lock-free ring buffer for low-latency systems workloads.

The primary goal is understanding real inter-core synchronization behavior under contention rather than only maximizing synthetic throughput numbers.

Current design goals:

* Fixed-size FIFO ring buffer.
* Lock-free synchronization for low-latency communication.
* Specialized SPSC and MPMC implementations.
* Compile-time configurable batching.
* Bulk enqueue/dequeue APIs.
* Cache-aware synchronization variable caching.
* Explicit cacheline alignment and false-sharing reduction.
* Publication-frequency tuning.
* SIMD-assisted payload movement for larger payloads.

> **Status:**
> SPSC paths are heavily optimized and benchmarked.
> MPMC paths exist but still require deeper scalability and stress validation.

---

# Queue Architecture

The implementation was later refactored into four specialized queue classes:

| Class | Purpose |
|---|---|
| `SPSCQueue<T>` | Low-latency SPSC queue |
| `SPSCQueueBatch<T>` | Throughput-oriented batched SPSC queue |
| `MPMCQueue<T>` | Multi-producer multi-consumer queue |
| `MPMCQueueBatch<T>` | Batched MPMC queue |

This separation avoids:
- leaking batching logic into latency paths
- leaking MPMC synchronization into SPSC hot paths
- unnecessary branching and template bloat
- instruction-cache pollution

At high optimization levels, simplifying generated machine code became more important than keeping a unified abstraction.

---

# Key Optimizations

## Cacheline Separation

Producer and consumer synchronization variables are isolated:

```cpp
alignas(64) std::atomic<uint32_t> prod_tail_;
alignas(64) std::atomic<uint32_t> cons_tail_;
```

This reduces:
- MESI invalidation traffic
- cacheline bouncing
- false sharing

---

## Power-of-Two Masking

Index wrapping uses:

```cpp
index & (capacity - 1)
```

instead of modulo operations to avoid division instructions in hot paths.

---

## Cached Remote Indices

Producer and consumer cache remote synchronization variables locally.

This reduces:
- acquire-load frequency
- coherence traffic
- shared cacheline reloads

This optimization became increasingly important on physically distant cores.

---

## SIMD Payload Copy Paths

Specialized AVX2 paths exist for 32-byte and 64-byte payloads:

```cpp
if constexpr (sizeof(T) == 32)
{
    _mm256_storeu_si256(...);
}
```

This reduces:
- scalar copy instructions
- dependency chains
- payload movement overhead

Performance impact depends heavily on:
- compiler backend quality
- payload alignment
- microarchitecture behavior

---
# Benchmark Results

# Throughput & Latency Benchmarks

The following results represent averages across 10 independent benchmark runs.

Threads were pinned to physically separate cores (`2 -> 4`) to expose realistic inter-core synchronization costs.

---

## Pointer Benchmark

| Implementation | Throughput (M ops/sec) |
|---|---|
| LFQueue | **121.0** |
| Drogalis | 106.2 |
| Rigtorp | 69.5 |
| BoostSPSC | 61.6 |

---

## Payload2 (2 bytes)

| Implementation | Throughput (M ops/sec) | Avg Latency | P50 | P99 |
|---|---|---|---|---|
| LFQueue | **80.6** | 502 | 528 | 556 |
| Rigtorp | 56.6 | 493 | 508 | 542 |
| Drogalis | 60.7 | 497 | 512 | 548 |
| BoostSPSC | 52.0 | 490 | 526 | 569 |

---

## Payload4 (4 bytes)

| Implementation | Throughput (M ops/sec) | Avg Latency | P50 | P99 |
|---|---|---|---|---|
| LFQueue | **115.9** | 488 | 520 | 539 |
| Rigtorp | 68.4 | 493 | 504 | 537 |
| Drogalis | 92.9 | 495 | 501 | 538 |
| BoostSPSC | 73.8 | 489 | 517 | 569 |

---

## Payload8 (8 bytes)

| Implementation | Throughput (M ops/sec) | Avg Latency | P50 | P99 |
|---|---|---|---|---|
| LFQueue | **110.1** | 499 | 527 | 549 |
| Rigtorp | 76.3 | 493 | 506 | 537 |
| Drogalis | 106.9 | 495 | 503 | 539 |
| BoostSPSC | 58.6 | 489 | 526 | 567 |

---

## Payload16 (16 bytes)

| Implementation | Throughput (M ops/sec) | Avg Latency | P50 | P99 |
|---|---|---|---|---|
| Drogalis | **81.0** | 499 | 518 | 546 |
| LFQueue | 70.3 | 487 | 525 | 546 |
| Rigtorp | 65.9 | 495 | 516 | 541 |
| BoostSPSC | 50.3 | 492 | 521 | 580 |

---

## Payload32 (32 bytes)

| Implementation | Throughput (M ops/sec) | Avg Latency | P50 | P99 |
|---|---|---|---|---|
| LFQueue | **72.6** | 380 | 368 | 446 |
| Rigtorp | 50.8 | 375 | 367 | 428 |
| Drogalis | 50.5 | 380 | 368 | 431 |
| BoostSPSC | 32.7 | 352 | 357 | 409 |

---

## Payload64 (64 bytes)

| Implementation | Throughput (M ops/sec) | Avg Latency | P50 | P99 |
|---|---|---|---|---|
| LFQueue | **45.3** | 404 | 390 | 492 |
| Drogalis | 36.0 | 397 | 388 | 474 |
| Rigtorp | 35.6 | 396 | 389 | 475 |
| BoostSPSC | 16.1 | 381 | 388 | 502 |

---

# Batch Throughput & Latency

| Benchmark | Result |
|---|---|
| LFQueueBatch Throughput | **412.6 M ops/sec** |
| Avg Batch Latency | 661 cycles |
| P50 Batch Latency | 640 cycles |
| P99 Batch Latency | 822 cycles |

---

# Burst Sweep Results

| Burst Size | Throughput (M ops/sec) |
|---|---|
| 1 | 25.8 |
| 2 | 52.2 |
| 4 | 109.4 |
| 8 | 165.4 |
| 16 | 324.0 |
| 32 | 442.2 |
| 64 | **582.3** |

### Observation

Burst throughput scales aggressively as synchronization overhead becomes amortized across larger batches.

This demonstrates that:
- atomic publication overhead dominates small bursts
- cacheline reuse improves significantly at larger bursts
- synchronization amortization becomes critical at high throughput levels

---

# Occupancy Sweep Results

| Queue Occupancy | Throughput (M ops/sec) |
|---|---|
| 25% | **143.0** |
| 50% | 119.0 |
| 75% | 131.0 |
| 95% | 127.5 |

### Observation

The queue maintains relatively stable throughput even under high occupancy levels, indicating good behavior near saturation and reduced degradation under backpressure.

---

# Publication Sweep Results

| Publish Interval | Throughput (M ops/sec) |
|---|---|
| 1 | 561.7 |
| 2 | 555.8 |
| 4 | 551.9 |
| 8 | 554.0 |
| 16 | 558.8 |
| 32 | 559.6 |
| 64 | **681.2** |

### Observation

Less frequent publication significantly reduces synchronization pressure and cache coherency traffic.

Publishing every 64 items produced the highest throughput by amortizing:
- acquire/release synchronization
- atomic store overhead
- cacheline ownership transfers

---

# Benchmark Methodology

The benchmark suite measures:

* hot-path throughput
* synchronization overhead
* cache coherency pressure
* inter-core communication behavior
* batching efficiency
* publication-frequency tradeoffs
* latency stability

The harness includes:

* warmup phases
* thread pinning
* cyclic payload pools
* repeated mean/median analysis
* burst sweeps
* occupancy sweeps
* publication-frequency sweeps

Threads are pinned to physically separate cores to expose realistic synchronization costs.

---

# Benchmark Configuration

| Setting | Value |
|---|---|
| Hardware | HP Laptop 15s |
| CPU | AMD Ryzen 5 |
| Memory | 16GB RAM |
| Operations | 50,000,000 |
| Queue Capacity | 65,536 |
| Build Flags | `-O3 -march=native -mavx2 -ftree-vectorize -fno-tree-slp-vectorize -flto` |
| Benchmark Runs | 10 averaged runs |
| CPU Governor | `performance` |
| Thread Pinning | Enabled |
| Warmup Phase | Enabled |

---

# CPU Isolation & Benchmark Environment

The following configuration was used during benchmarking:

```bash
sudo cpupower frequency-set -g performance
```

Edit GRUB:

```bash
sudo nano /etc/default/grub
```

Replace:

```bash
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash"
```

with:

```bash
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash isolcpus=2,4 nohz_full=2,4 rcu_nocbs=2,4"
```

Apply:

```bash
sudo update-grub
sudo reboot
```

Disable boost:

```bash
echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost
```

Disable SMT:

```bash
cat /sys/devices/system/cpu/smt/control
echo off | sudo tee /sys/devices/system/cpu/smt/control
```

This reduces:
- scheduler noise
- SMT sibling interference
- frequency oscillation
- kernel tick interruptions

---



# What The Benchmarks Actually Measure

## Throughput Benchmarks

Measures:
- enqueue/dequeue hot-path cost
- synchronization overhead
- cache coherency traffic
- payload movement efficiency

This is primarily:
- a synchronization benchmark
- a cache coherency benchmark
- a compiler code-generation benchmark

not a pure memory-bandwidth benchmark.

---

## Latency Benchmarks

Measures:
- inter-core roundtrip latency
- publication visibility delay
- tail latency stability

Metrics:
- average latency
- p50 latency
- p99 latency

Latency is measured using `rdtscp`.

---

## Burst Sweeps

Burst sizes:
- 1
- 2
- 4
- 8
- 16
- 32
- 64

Measures:
- synchronization amortization
- batching scalability
- cacheline reuse efficiency

Small bursts:
- lower latency
- higher synchronization frequency

Large bursts:
- higher throughput
- reduced atomic overhead

---

## Publication Sweeps

Varies producer publication frequency.

Measures:
- acquire/release overhead
- cache coherency traffic
- synchronization frequency cost

Publishing less frequently:
- improves throughput
- reduces coherence traffic

but increases visibility delay.

---

## Occupancy Sweeps

Queue is prefilled to:
- 25%
- 50%
- 75%
- 95%

Measures:
- backpressure behavior
- near-full contention
- saturation degradation

Many queues benchmark well when empty but degrade near capacity.

---

# Important Observations

## SMT Sibling Measurements Were Misleading

Earlier benchmarks used SMT siblings:

```text
0 -> 1
```

which shared:
- L1 cache
- store buffers
- execution resources

This produced artificially optimistic results.

Current benchmarks use physically separate cores:

```text
0 -> 2
```

to expose real inter-core synchronization costs.

---

## Benchmark Harnesses Can Become Bottlenecks

Earlier benchmark versions accidentally measured:
- payload construction
- allocation overhead
- memory initialization

instead of queue performance itself.

This was fixed using:
- cyclic payload pools
- warmup phases
- pre-generated payload buffers

---

## CPU Isolation Misconfiguration Can Destroy Performance

Incorrect `isolcpus` / `nohz_full` usage caused isolated CPUs to enter deep idle states.

This collapsed throughput from:
- ~300M ops/sec
to:
- below 100M ops/sec

despite relatively low instruction counts.

This reinforced a major lesson:

> Benchmark environment quality matters as much as algorithm quality.

---

# Current Optimizations

Implemented optimizations include:

* Cached synchronization variables
* Reduced synchronization frequency
* Compile-time batching
* Bulk enqueue/dequeue APIs
* Cacheline-separated synchronization state
* Branch prediction hints
* `always_inline` hot paths
* Power-of-two masking
* SIMD-assisted payload copies
* Publication amortization
* Payload pre-generation pools
* Warmup-aware benchmarking

---

# Future Work

Planned improvements:

* NUMA-aware allocation
* Hugepage-backed rings
* ARM NEON optimizations
* Adaptive batching
* Non-temporal stores
* Prefetch tuning
* perf c2c analysis
* Store-buffer pressure analysis
* Hybrid spin/backoff strategies
* Better MPMC scalability tuning

---

# Running Benchmarks

```bash
make run
```

Override CPU placement:

```bash
make run PRODUCER_CORE=0 CONSUMER_CORE=2
```

Using physically separate cores is strongly recommended.

---

# ThreadSanitizer Validation

```bash
make check-sanity
```

Used for:
- synchronization validation
- memory-ordering verification
- race detection

---

# Conclusion

This project evolved from a lock-free queue implementation into a broader investigation of:

* low-latency synchronization
* cache coherency behavior
* compiler backend effects
* benchmarking methodology
* inter-core communication

At sufficiently optimized levels, performance became dominated by:
- cache topology
- coherence traffic
- synchronization frequency
- compiler code generation
- benchmark quality

rather than only queue algorithm design.