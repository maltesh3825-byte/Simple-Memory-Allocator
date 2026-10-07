# Custom Memory Allocator (`myalloc`)

A lightweight, educational, 16-byte aligned dynamic memory allocator (`malloc`/`free` clone) implemented from scratch in C11. Built for a 12-hour hackathon, this project demonstrates foundational systems programming concepts: managing a raw byte buffer, an all-blocks address-ordered doubly linked list, block splitting, immediate neighbor coalescing, and allocation heuristics (First-Fit vs. Best-Fit).

---

## Table of Contents
1. [Overview](#overview)
2. [How It Works](#how-it-works)
   - [The Static Heap](#the-static-heap)
   - [Block Header & Alignment](#block-header--alignment)
   - [All-Blocks Address-Ordered Doubly Linked List](#all-blocks-address-ordered-doubly-linked-list)
   - [Lazy Initialization](#lazy-initialization)
   - [Block Splitting](#block-splitting)
   - [O(1) Immediate Neighbor Coalescing](#o1-immediate-neighbor-coalescing)
   - [Allocation Strategies: First-Fit vs. Best-Fit](#allocation-strategies-first-fit-vs-best-fit)
3. [Project Layout](#project-layout)
4. [Build and Run Instructions](#build-and-run-instructions)
5. [Diagnostics & Introspection API](#diagnostics--introspection-api)
6. [Design Trade-offs, Caveats & Limitations](#design-trade-offs-caveats--limitations)

---

## Overview

In C, the standard library provides `malloc()` and `free()` to manage the heap. On modern operating systems, production allocators request large virtual memory arenas from the OS kernel via system calls such as `brk()`, `sbrk()`, or `mmap()`, then manage them with complex size-segregated bins and thread-local caches.

`myalloc` focuses on **simplicity and educational clarity**. Instead of managing kernel address spaces, it manages a dedicated **1 MB static array** (`_Alignas(16) uint8_t heap[1024 * 1024]`), tracking chunks via an in-place doubly linked list containing all blocks (both allocated and free) in physical address order.

---

## How It Works

### The Static Heap
Rather than expanding process boundaries with `sbrk()`, our allocator operates within a pre-allocated 1 MB buffer:
```c
static _Alignas(16) uint8_t heap[HEAP_SIZE]; // 1,048,576 bytes
```
- `_Alignas(16)` guarantees that the base address of the heap array starts on a 16-byte aligned boundary.
- All block headers and returned payloads strictly maintain this 16-byte alignment invariant.

### Block Header & Alignment
Every memory block (whether free or allocated) consists of a **metadata header** followed immediately by the **user payload**:

```
+------------------------------------+---------------------------------------+
| struct Block Header (32 bytes)     | User Payload (16-byte aligned)        |
| - size: payload size (bytes)       |                                       |
| - free: 1 (free) or 0 (used)       |                                       |
| - next: pointer to next Block      |                                       |
| - prev: pointer to prev Block      |                                       |
+------------------------------------+---------------------------------------+
^                                    ^
Block Address                        Returned User Pointer (ptr)
```

The header definition in `include/myalloc.h`:
```c
struct Block {
    size_t size;         /* Usable payload size in bytes (excludes header) */
    int free;            /* 1 if block is free, 0 if allocated */
    struct Block *next;  /* Physically next block in address order */
    struct Block *prev;  /* Physically previous block in address order */
};
```

On 64-bit systems:
- `size_t size` (8 bytes) + `int free` (4 bytes) + padding (4 bytes) + `next` (8 bytes) + `prev` (8 bytes) = **32 bytes**.
- Because 32 is a multiple of 16, adding the header to an aligned block address guarantees that the returned payload pointer is also 16-byte aligned.
- Any requested `size` is rounded up using:
  `ALIGN_SIZE(size) = (size + 15) & ~15`.

### All-Blocks Address-Ordered Doubly Linked List
Instead of maintaining an explicit free list (which chains only unallocated blocks), `myalloc` links **all blocks** (both free and used) in a single doubly linked list sorted strictly by **physical memory address**:
- `heap_head` points to the block at the lowest address (`heap[0]`).
- For any block `B`, `B->next` is the physically adjacent block higher in memory.
- `B->prev` is the physically adjacent block lower in memory.

### Lazy Initialization
The allocator initializes itself on the first call to `my_malloc()`, converting the entire 1 MB buffer into a single large free block:
- `heap_head->size = 1048576 - 32 = 1,048,544 bytes`
- `heap_head->free = 1`
- `heap_head->next = NULL`
- `heap_head->prev = NULL`

### Block Splitting
When an allocation request is smaller than the chosen free block, `myalloc` carves out the requested bytes and splits the remainder into a new free block:
1. Ensure the leftover space can hold a header (32 bytes) plus at least 16 bytes of payload (`leftover >= 48 bytes`).
2. Construct a new `struct Block` at `(char *)curr_payload + req_size`.
3. Set `new_block->size = curr->size - req_size - 32` and `new_block->free = 1`.
4. Splice `new_block` into the linked list between `curr` and `curr->next`.
5. Set `curr->size = req_size` and `curr->free = 0`.

### O(1) Immediate Neighbor Coalescing
When a block is released via `my_free(ptr)`, neighboring free blocks are merged together. Because the list is ordered strictly by physical memory address (and lacks footers, unlike classic Knuth boundary tags):
- **Right neighbor merge**: If `block->next` is free, add `32 + block->next->size` to `block->size` and update `block->next = block->next->next`.
- **Left neighbor merge**: If `block->prev` is free, add `32 + block->size` to `block->prev->size` and update `block->prev->next = block->next`.
- Both checks happen in **O(1) constant time** with zero list traversal.

### Allocation Strategies: First-Fit vs. Best-Fit
Switchable at runtime via `my_set_strategy()`:
- **First-Fit (`FIRST_FIT`)**: Traverses the list from `heap_head` and selects the first free block where `size >= req_size`. Fast because it stops early.
- **Best-Fit (`BEST_FIT`)**: Scans the entire heap from start to finish, selecting the free block whose size is closest to `req_size`. Minimizes leftover slack from splitting, but requires walking all blocks.

---

## Project Layout

```
pb-hackathon/
├── include/
│   └── myalloc.h                 # Public API declarations and Block struct definition
├── src/
│   └── myalloc.c                 # Core allocator logic (malloc, free, split, coalesce)
├── tests/
│   └── test_alloc.c              # 9 unit tests (deterministic PRNG and alignment checks)
├── bench/
│   ├── bench.c                   # 5-run median benchmark with warm-up & saturation test
│   ├── seed_sweep.c              # 300-seed validation tool for saturation workload
│   └── results.csv               # Exported performance dataset
├── report/
│   ├── REPORT.md                 # Technical report with empirical results and analysis
│   └── generate_report_table.py  # Automation script syncing CSV to REPORT.md table
├── Makefile                      # Build automation (all, test, bench, report, valgrind, clean)
├── .gitignore                    # Ignores build outputs and compiled binaries
└── README.md                     # Complete project documentation
```

---

## Build and Run Instructions

### Prerequisites
- GCC compiler supporting `-std=c11`
- GNU Make
- Linux / WSL or Windows (MinGW-w64)

### Targets

1. **Build Everything**
   ```bash
   make all
   ```

2. **Run Unit Test Suite**
   ```bash
   make test
   ```
   Runs 9 unit test cases against both `FIRST_FIT` and `BEST_FIT` (18 total runs) using a portable 32-bit PRNG.

3. **Run Performance Benchmarks**
   ```bash
   make bench
   ```
   Executes four workloads (batch allocations, random churn, checkerboard fragmentation, and saturation fill-until-failure) across First-Fit, Best-Fit, and system malloc. Reports median timings across 5 runs and writes to `bench/results.csv`.

4. **Update Report Table from CSV**
   ```bash
   make report
   ```
   Executes `report/generate_report_table.py` to synchronize `bench/results.csv` into the markdown results table in `report/REPORT.md`.

5. **Run Memory Leak Verification**
   ```bash
   make valgrind
   ```
   Runs test suite under Valgrind. *(See Valgrind Caveat below).*

6. **Run Address and Undefined Behavior Sanitizers**
   ```bash
   make sanitize
   ```
   Builds the test suite with AddressSanitizer and UndefinedBehaviorSanitizer
   enabled, then runs all allocator tests.

7. **Clean Build Outputs**
   ```bash
   make clean
   ```

On Windows with MinGW, run these commands from a MinGW-enabled shell. The
Makefile automatically adds `.exe` to generated binaries and uses PowerShell
for cleanup.

---

## Diagnostics & Introspection API

`myalloc` provides built-in introspection tools:
- `size_t my_total_free(void)`: Returns total free payload bytes across all free chunks.
- `size_t my_largest_free(void)`: Returns the single largest contiguous free block size.
- `double my_fragmentation(void)`: Computes external fragmentation:
  $$\text{Fragmentation} = 1.0 - \frac{\text{largest\_free}}{\text{total\_free}}$$
  (0.0 = completely unfragmented; close to 1.0 = severely fragmented).
- `void my_dump_heap(void)`: Prints a visual ASCII table of every block's address, payload pointer, size, and status.

---

## Design Trade-offs, Caveats & Limitations

1. **Fixed 1 MB Heap Capacity**: The heap size is static (1 MB) and does not expand via `sbrk()` or `mmap()`.
2. **Metadata Overhead**: Every allocation carries a 32-byte header. For small payloads, this represents 100%–200% relative overhead on 16–32 byte chunks (and 50% on a 64-byte chunk). Production allocators avoid this by using segregated slab caches with external bitmap headers.
3. **Best-Effort Double-Free Detection**: Double-free detection validates that `block->free == 0` before freeing. However:
   - Once a block is freed and coalesced into an adjacent neighbor, its old header becomes part of the neighbor's payload or is absorbed.
   - Subsequent user writes to that neighboring payload may overwrite the header data.
   - Therefore, double-free detection is strictly best-effort for immediate frees, not an absolute guarantee across all states.
4. **Valgrind Visibility Caveat**: Because `myalloc` distributes slices of a static array in `.bss`, Valgrind Memcheck treats the entire 1 MB buffer as valid memory. Without embedding Valgrind client requests (`VALGRIND_MALLOCLIKE_BLOCK` from `<valgrind/valgrind.h>`), Valgrind cannot detect internal memory leaks, use-after-free, or buffer overflows within the static heap. Passing Valgrind confirms clean test-harness memory hygiene, not internal allocator boundary enforcement.
5. **Single Doubly Linked List Traversal**: Both allocated and free blocks reside in the same list. Free block search takes $O(N)$ time where $N$ is total block count, unlike segregated lists where search is $O(1)$.
6. **Thread Safety & Extensions**: Out of scope for this hackathon project; lacks mutex synchronization, and does not implement `realloc` or `calloc`.
