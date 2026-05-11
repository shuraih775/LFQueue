# LFQueue

Lock-free ring queue implementation inspired by DPDK `rte_ring`.

Focused mainly on understanding synchronization, cache coherency, batching, and compiler/codegen behavior in low-latency systems.

Current status:

* optimized SPSC paths
* experimental/tunable MPMC paths

---

# Queue Types

| Queue               | Purpose                         |
| ------------------- | ------------------------------- |
| `SPSCQueue<T>`      | single producer single consumer |
| `SPSCQueueBatch<T>` | batched SPSC                    |
| `MPMCQueue<T>`      | multi producer multi consumer   |
| `MPMCQueueBatch<T>` | batched MPMC                    |

---

# Optimizations

* cacheline separation
* power-of-two masking
* cached remote indices
* AVX2 payload copies
* batching
* reduced publication frequency
* branch prediction hints
* `always_inline` hot paths
* bulk enqueue/dequeue
* warmup-aware benchmarking

---

# Benchmarks

Benchmarks were run on physically separate cores (`2 -> 4`) with thread pinning enabled.

## Throughput
Median throughput across 10 runs:

| Payload | LFQueue   | Drogalis | Rigtorp | BoostSPSC |
| ------- | --------- | -------- | ------- | --------- |
| Pointer | **121.0** | 106.2    | 69.5    | 61.6      |
| 2B      | **120.6** | 106.7    | 66.6    | 62.0      |
| 4B      | **115.9** | 92.9     | 68.4    | 73.8      |
| 8B      | **110.1** | 91.9     | 76.3    | 58.6      |
| 16B     | **70.3**  | 70.1     | 65.9    | 50.3      |
| 32B     | **72.6**  | 50.5     | 50.8    | 32.7      |
| 64B     | **45.3**  | 36.0     | 35.6    | 16.1      |

Units: M ops/sec

## Latency

Measured using `rdtscp` ping-pong benchmarks on pinned physical cores (`2 -> 4`). (avg of 10 runs)

Median latency (cycles):

| Payload | LFQueue | Drogalis | Rigtorp |
| ------- | ------- | -------- | ------- |
| 2B      | 528     | 512      | 508     |
| 4B      | 520     | 501      | 504     |
| 8B      | 527     | 503      | 506     |
| 16B     | 525     | 518      | 516     |
| 32B     | 368     | 368      | 367     |
| 64B     | 390     | 388      | 389     |



## Batch Throughput (for lfqueue)

| Benchmark             | Result              |
| --------------------- | ------------------- |
| Batch Throughput      | **412.6 M ops/sec** |
| Best Burst Sweep      | **582.3 M ops/sec** |
| Best Publish Interval | **681.2 M ops/sec** |


## Benchmark Environment

| Setting        | Value                            |
| -------------- | -------------------------------- |
| CPU            | Ryzen 5                          |
| RAM            | 16GB                             |
| Queue Capacity | 65536                            |
| Operations     | 50M                              |
| Compiler Flags | `-O3 -march=native -mavx2 -ftree-vectorize -fno-tree-slp-vectorize -flto` |
| Thread Pinning | enabled                          |
| Warmup         | enabled                          |

---

# Running

Build + run GCC benchmarks:

```bash
make run
```

Build + run Clang benchmarks:

```bash
make run-clang
```

Custom core placement:

```bash
make run PRODUCER_CORE=2 CONSUMER_CORE=4
```

```bash
make run-clang PRODUCER_CORE=2 CONSUMER_CORE=4
```

Avoid SMT sibling pairs like:

```text
0 -> 1
```

Use physically separate cores instead:

```text
2 -> 4
```

---

# Sanitizer Check

```bash
make check-sanity
```

---

# perf / Assembly

LFQueue:

```bash
make lf-run
make perf-lf
make lf-asm
```

Drogalis:

```bash
make dro-run
make perf-dro
make dro-asm
```

Rigtorp:

```bash
make rigtorp-run
make perf-rigtorp
make rigtorp-asm
```
