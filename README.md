## Design & Inspiration

This project is inspired by the **DPDK `rte_ring`** architecture, focused on achieving:

* **FIFO** ordering with a **fixed-size** pointer table.
* **Lockless** synchronization for ultra-low latency.
* **SP/MP & SC/MC** support (Single/Multi-Producer and Consumer).
* **Bulk API** for high-efficiency enqueue/dequeue.

> **Status:** Optimized for SPSC. MP/MC logic is being implemented and currently is not tested.

## Future Optimizations

The following enhancements are planned to further align with DPDK performance standards:

* Analysis and improvements to be done for enqueue/dequeue ops at assembly level w.r.t. `mov` ops to improve the efficiency of moving data in and out of the queue. 
* **Adaptive Batching** 
* **Hugepage Support** 
* **NUMA Awareness** 
* **ARM Neoverse/AArch64**
 
---

## Performance Benchmarks

The following results represent the **average of 10 independent runs** on an HP Laptop 15s. Tests were conducted using 50 million operations per run, with threads pinned to physical cores to ensure consistent inter-core communication.

| Implementation | Throughput (M ops/sec) | Avg Latency (Cycles) | P50 Latency | P99 Latency |
| --- | --- | --- | --- | --- |
| **MutexQueue** | 4.63 | 2227 | 1775 | 8476 |
| **NaiveRing** | 124.11 | 66 | 63 | 81 |
| **LFQueue (Ours)** | **238.29** | **73** | **73** | **84** |
| **Rigtorp** | 251.10 | 70 | 73 | 84 |
| **Drogalis** | 265.82 | 71 | 73 | 84 |
| **Boost SPSC** | 156.45 | 70 | 71 | 84 |
| **MoodyCamel** | 33.36 | 133 | 126 | 210 |


### Benchmark Configuration

* **Hardware:** HP Laptop 15s with AMD Ryzen 5 (16GB RAM)
* **Operations:** 50,000,000 per run
* **Ring Capacity:** 2^16 (65,536) entries
* **Thread Pinning:** Producer (Core 0), Consumer (Core 1) via `taskset`
* **Build Profile:** `-O3 -march=native -flto` (LTO enabled for cross-module optimization)
* **Warmup:** 1,000,000 ops prior to measurement to stabilize CPU frequency.
* **Power Frequency:** Set to `performance`

---

## Testing & Validation

### 1. Build Requirements

* **CMake** (3.10+) & **Ninja**
* **G++** (11+) or **Clang**

### 2. Running Benchmarks

We use `taskset` to pin threads to specific cores. This is critical to measure true inter-core latency without OS interference.

```bash
# Using the provided Makefile
make run

```

*Note: Default pinning is Core 0 and 1. You can override this via `make run PRODUCER_CORE=0 CONSUMER_CORE=2`.*

### 3. Running Sanity Checks (TSan)

To verify thread safety and memory ordering, we use the ThreadSanitizer. We execute with ASLR disabled to ensure compatibility with modern Linux kernels.

```bash
make check-sanity

```