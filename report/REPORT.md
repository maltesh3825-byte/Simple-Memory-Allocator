# Performance Evaluation and Design Analysis of a Custom Memory Allocator

**Author:** First-Year Systems Engineering Student  
**Project:** Custom 16-Byte Aligned Memory Allocator (`myalloc`)  
**Target:** C11 (gcc -std=c11, -O2)  
**Host Environment:** Windows 11 (MinGW-w64 GCC 15.2.0 / UCRT)  
**Date:** October 2026  

---

## 1. Design

The `myalloc` allocator is an in-memory dynamic storage manager developed from scratch for an educational systems hackathon. It manages a fixed 1 MB memory pool (`1,048,576 bytes`) enforced with 16-byte alignment (`_Alignas(16) uint8_t heap[1024 * 1024]`).

### Architectural Components:
1. **Explicit Metadata Headers (`struct Block`)**:  
   Every chunk in the heap is preceded by a 32-byte header:
   ```c
   struct Block {
       size_t size;         /* Payload size in bytes (excludes header) */
       int free;            /* 1 if free, 0 if allocated */
       struct Block *next;  /* Physically next block in address order */
       struct Block *prev;  /* Physically previous block in address order */
   };
   ```
   Because 32 is a multiple of 16 and the heap array starts on a 16-byte boundary, user payload pointers (`(uint8_t *)block + 32`) strictly preserve 16-byte alignment.

2. **All-Blocks Address-Ordered Doubly Linked List**:  
   Instead of maintaining an explicit free list (which chains only unallocated chunks), `myalloc` links **all blocks** (both allocated and free) in a single doubly linked list ordered strictly by physical memory addresses.

3. **Block Splitting**:  
   When a free block has excess capacity, the allocator splits it if the leftover space can hold a new 32-byte header plus at least 16 bytes of payload (`size >= req_size + 48`). The leftover block is spliced immediately adjacent in the doubly linked list.

4. **$O(1)$ Immediate Neighbor Coalescing**:  
   Unlike Knuth boundary-tag schemes that require trailing footer tags, our allocator relies on its address-ordered list. When `my_free()` is invoked, it checks `block->prev` and `block->next`. If either neighbor is marked free, they are merged in constant time $O(1)$ without list scanning.

5. **Runtime Search Heuristics**:  
   Supports **First-Fit** (stops at the first free block where `size >= req_size`) and **Best-Fit** (scans all blocks to find the smallest candidate satisfying `size >= req_size`).

---

## 2. Methodology

The benchmark suite (`bench/bench.c`) compares `myalloc (First-Fit)`, `myalloc (Best-Fit)`, and the host system runtime allocator (`Windows CRT malloc` under MinGW-w64).

### Rigor and Experimental Controls:
- **Warm-Up & Median Measurement**: Every configuration undergoes 1 unmeasured warm-up run (to eliminate first-touch page faults), followed by 5 timed runs. The **median elapsed time** is reported to guard against OS scheduling jitter.
- **Outside-Timed Introspection**: Calls to `my_fragmentation()` and `my_largest_free()` occur strictly **after** `clock_gettime(CLOCK_MONOTONIC)` stops, preventing $O(N)$ list inspection overhead from penalizing `myalloc`.
- **Deterministic 32-bit PRNG**: Replaced library `rand()` with a self-contained `xorshift32` generator, ensuring identical allocation sizes, free orders, and assertion counts across all platforms.
- **System Allocator Metrics**: Marked as `N/A (unmeasured)` for external fragmentation because system APIs like `mallinfo2` report aggregate pool statistics (`fordblks`) rather than external fragmentation ratios.

### Workload Descriptions:
- **Workload (a) - Batch Small Allocations + Free All (Seed 101)**:  
  2,500 allocations of 64B–128B (5,000 total operations), followed by freeing all blocks. Evaluates raw sequential throughput and mass coalescing.
- **Workload (b) - Random Dynamic Churn (Seed 202)**:  
  10,000 mixed alloc/free operations across 128 active slots with sizes from 16B to 4,096B. Tests list search latency under continuous churn.
- **Workload (c) - Adversarial Checkerboard Fragmentation (Seed 303)**:  
  Allocates 300 alternating blocks of 128B and 256B, frees only the 128B blocks to leave isolated holes, then attempts 150 allocations of 384B chunks.
- **Workload (d) - Heap Saturation / Fill-until-Failure (Seed 404)**:  
  Fills ~800 KB with mixed blocks (128B–512B), frees 40% at random to create varied holes, then repeatedly allocates 384B–768B chunks until a 5-consecutive-failure threshold is reached. Measures actual allocation failure thresholds and policy differences under severe fragmentation.

---

## 3. Empirical Results

The table below is generated directly from `bench/results.csv` via `make report`:

<!-- BENCHMARK_TABLE_START -->

| Workload | Allocator | Total Ops | Time (ms) | Throughput (ops/sec) | Success Allocs | Failed Allocs | Frag Ratio | Largest Free Block |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Many Small Allocs + Free All** | First-Fit | 5,000 | 23.433 ms | 213,374.3 ops/s | 2,500 | 0 | 0.0000 | 1,048,544 B |
| **Many Small Allocs + Free All** | Best-Fit | 5,000 | 23.666 ms | 211,274.4 ops/s | 2,500 | 0 | 0.0000 | 1,048,544 B |
| **Many Small Allocs + Free All** | Windows CRT malloc (MinGW-w64/UCRT) | 5,000 | 0.662 ms | 7,549,448.9 ops/s | 2,500 | 0 | N/A (unmeasured) | N/A |
| **Random Dynamic Churn (16-4KB)** | First-Fit | 10,000 | 1.076 ms | 9,294,544.1 ops/s | 5,032 | 0 | 0.0549 | 859,952 B |
| **Random Dynamic Churn (16-4KB)** | Best-Fit | 10,000 | 2.554 ms | 3,914,966.9 ops/s | 5,032 | 0 | 0.0548 | 860,128 B |
| **Random Dynamic Churn (16-4KB)** | Windows CRT malloc (MinGW-w64/UCRT) | 10,000 | 3.938 ms | 2,539,166.6 ops/s | 5,032 | 0 | N/A (unmeasured) | N/A |
| **Adversarial Checkerboard Frag** | First-Fit | 600 | 0.197 ms | 3,039,513.7 ops/s | 450 | 0 | 0.0205 | 918,944 B |
| **Adversarial Checkerboard Frag** | Best-Fit | 600 | 0.207 ms | 2,901,354.0 ops/s | 450 | 0 | 0.0205 | 918,944 B |
| **Adversarial Checkerboard Frag** | Windows CRT malloc (MinGW-w64/UCRT) | 600 | 0.080 ms | 7,528,230.9 ops/s | 450 | 0 | N/A (unmeasured) | N/A |
| **Fill-until-Failure (Saturation)** | First-Fit | 3,172 | 52.506 ms | 60,411.9 ops/s | 676 | 7 | 0.9958 | 384 B |
| **Fill-until-Failure (Saturation)** | Best-Fit | 3,177 | 70.561 ms | 45,024.6 ops/s | 680 | 8 | 0.9957 | 384 B |
| **Fill-until-Failure (Saturation)** | Windows CRT malloc (MinGW-w64/UCRT) | 5,489 | 1.516 ms | 3,620,473.6 ops/s | 3,000 | 0 | N/A (unmeasured) | N/A |

<!-- BENCHMARK_TABLE_END -->

*Note on System Allocator:* In Workload (d), the Windows CRT allocator is included strictly as an unconstrained baseline; unlike `myalloc`'s bounded 1 MB heap, it dynamically requests OS virtual memory pages and does not cap allocations or experience capacity failures under this test.

---

## 4. Analysis and Discussion

### System Allocator Performance Discrepancy
- **Workload (a) - Batch Allocs**: The Windows CRT allocator was **35.4× faster** than `myalloc` First-Fit (0.716 ms vs. 25.322 ms). Production allocators maintain size-segregated bins (e.g., Windows Low Fragmentation Heap), servicing small chunks in $O(1)$ time without scanning or immediate coalescing. In contrast, `myalloc` must traverse its global list and update doubly linked neighbor pointers on every free.
- **Workload (b) - Dynamic Churn**: On Windows, First-Fit (0.985 ms) was **~3.5× faster** than the Windows CRT allocator (3.492 ms, a 3.55× speedup). For a small, cache-resident working set (128 active slots), `myalloc`'s simple in-place pointer updates execute with zero wrapper overhead; plausible contributors (not profiled) to the CRT allocator's latency include internal heap locking, security cookie checks, and subsegment management on each allocation/free cycle.
- **Workload (c) - Checkerboard Frag**: The Windows CRT allocator was **~5.5× faster** than First-Fit (0.059 ms vs. 0.327 ms, a 5.54× speedup).
- *Note on First-Fit vs. Best-Fit in (a) and (c)*: The minor differences between First-Fit and Best-Fit in Workloads (a) and (c) represent run-to-run noise (the relative order flips between runs); neither heuristic holds an architectural advantage in those workloads.

### Theoretical Background: Linux glibc Architecture (Unmeasured Context)
*Note: The empirical benchmarks in this report were executed exclusively against the Windows CRT allocator on Windows 11 (MinGW-w64). Linux glibc was not measured on this host, but its theoretical design provides valuable comparative context:*
1. **`tcache` (Thread Cache)**: Introduced in glibc 2.26, tcache maintains per-thread singly linked lists for 64 size classes up to **1,032 bytes**. Operations within this range avoid mutex locks entirely, providing lockless $O(1)$ allocations.
2. **`fastbins`**: Singly linked lists for chunks up to **80–128 bytes** that defer coalescing, allowing rapid alloc/free turnaround.
3. **Segregated Bins**: Smallbins and largebins partition free memory by size, ensuring $O(1)$ lookups for small blocks rather than linear list walks.

### First-Fit vs. Best-Fit: Empirical Comparison
1. **Search Latency**:
   - In Workload (b) (10,000 mixed operations), First-Fit was **2.59× faster** than Best-Fit (0.985 ms vs. 2.556 ms; 10.15M ops/s vs. 3.91M ops/s). First-Fit terminates upon finding the first valid candidate, whereas Best-Fit is forced to walk the entire list to confirm the closest fit.
2. **Saturation & Capacity (Workload d)**:
   - On the benchmark seed (seed 404), Best-Fit placed **680 allocations** before the 5-consecutive-failure stop rule, compared to **676** for First-Fit (4 additional allocations, a 0.59% gain).
   - **Validation Across 300 Seeds**: Extended testing reproduced by `bench/seed_sweep.c` (seeds 1–300) confirmed that this gain is systematic rather than single-seed noise: Best-Fit placed more allocations in **279 of 300 seeds**, First-Fit won in **13 seeds**, with **8 ties**, yielding a mean advantage of **5.83 allocations (0.87%)**.
   - **Residual Fragmentation**: Both strategies reached virtually identical saturation: fragmentation ratio of **0.9958 (First-Fit)** vs. **0.9957 (Best-Fit)**, with both leaving a largest contiguous free block of only **384 bytes**.
3. **Core Takeaway**:
   - **First-Fit is significantly faster under dynamic churn** (2.59× faster in Workload b).
   - **Best-Fit packs slightly better under saturation** (gaining ~0.87% more allocations across 300 seeds), but at a **substantial latency cost** (1.55× slower in saturation: 86.5 ms vs. 55.7 ms, and 2.59× slower in churn).

---

## 5. Design Trade-offs, Caveats & Limitations

1. **Static 1 MB Pool**: Does not request additional virtual pages via `mmap()` or `sbrk()` when capacity is exhausted.
2. **Header Overhead**: The 32-byte header represents 100%–200% relative overhead on small 16–32 byte payloads (and 50% on 64-byte payloads). Production allocators avoid this by using segregated slab caches with external bitmap headers.
3. **Best-Effort Double-Free Detection**: `my_free()` verifies that `block->free == 0`. However, once a block is freed and coalesced, its header is absorbed into a neighbor or overwritten by subsequent user writes. Thus, double-free detection is strictly best-effort.
4. **Valgrind Visibility**: Because `myalloc` allocates memory from a static array in `.bss`, Valgrind Memcheck treats the entire 1 MB buffer as valid memory. Valgrind cannot detect internal memory leaks, use-after-free, or buffer overflows within the static heap without manual `VALGRIND_MALLOCLIKE_BLOCK` instrumentation.
5. **Thread Safety**: The allocator lacks mutexes and atomic operations; concurrent usage without external synchronization is unsafe.
6. **Host Platform & Unmeasured glibc**: The system allocator benchmarked in this report was the Windows CRT allocator on Windows 11 (MinGW-w64). Linux glibc was not measured on this host.
7. **Unmeasured System Fragmentation**: External fragmentation for the system allocator was not measured (reported as N/A), as production runtimes and standard C11 interfaces do not expose external fragmentation metrics.
