
## Design & Inspiration

This project is inspired by the **DPDK `rte_ring`** architecture, focused on achieving:

* **FIFO** ordering with a **fixed-size** pointer table.
* **Lockless** synchronization for ultra-low latency.
* **SP/MP & SC/MC** support (Single/Multi-Producer and Consumer).
* **Bulk API** for high-efficiency enqueue/dequeue.

> **Status:** Optimized for SPSC. MP/MC logic is implemented but currently is not heavily tested.

## Future Optimizations

The following enhancements are planned to further align with DPDK performance standards:

* **Adaptive Batching:** 
* **Hugepage Support:** 
* **NUMA Awareness:** 
* **ARM Neoverse/AArch64:**
---

## Performance Benchmarks

The following results represent the **average of 10 independent runs** on an HP Laptop 15s. Tests were conducted using 50 million operations per run, with threads pinned to physical cores to ensure consistent inter-core communication.

| Implementation | Avg. Throughput | Performance Gain |
| --- | --- | --- |
| **MutexQueue** (Baseline) | **5.04 M ops/sec** | 1x |
| **Naive Ring Buffer** | **111.09 M ops/sec** | ~22x |
| **Optimized Ring** | **252.36 M ops/sec** | **~50x** |

### Benchmark Configuration

* **Hardware:** HP Laptop 15s with AMD Ryzen 5 (16GB RAM)
* **Operations:** 50,000,000 per run
* **Ring Capacity:** 2^16 (65,536) entries
* **Thread Pinning:** Producer (Core 0), Consumer (Core 1) via `taskset`
* **Build Profile:** `-O3 -march=native -flto` (LTO enabled for cross-module optimization)
* **Warmup:** 1,000,000 ops prior to measurement to stabilize CPU frequency.

### Analysis

The **Optimized Ring** achieves a **~50x speedup** over the mutex-based implementation. While the Naive Ring suffers from constant cache-line contention (false sharing), the Optimized Ring leverages **Local State Caching** and **Publish Batching** to minimize cache coherency traffic. In optimal conditions, the implementation sustained peaks of **277 M ops/sec**, effectively operating at the theoretical limit of the CPU's L2 cache interconnect.

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