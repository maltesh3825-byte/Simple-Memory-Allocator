/**
 * @file bench.c
 * @brief Benchmark suite comparing custom allocator (First-Fit, Best-Fit) with
 * system malloc.
 *
 * Workloads:
 *  (a) Batch Small Allocations: Bulk allocate many small blocks (64-128B), then
 * free all. (b) Random Dynamic Churn: 10,000 mixed random alloc/free operations
 * (16B - 4096B). (c) Adversarial Checkerboard Fragmentation: Striped alloc/free
 * followed by medium allocations. (d) Heap Saturation / Fill-until-Failure:
 * Pre-fragment heap with varied holes, then allocate larger chunks until
 * failure to test allocator resilience and block selection under pressure.
 *
 * Methodology Improvements:
 *  - Portable 32-bit PRNG (xorshift32) guarantees identical allocation
 * sequences on all platforms.
 *  - Warm-up pass prior to measurement to eliminate first-touch page fault
 * anomalies.
 *  - 5 measurement iterations per workload reporting the median elapsed time.
 *  - Introspection metrics (fragmentation ratio, largest free) are measured
 * OUTSIDE the timed region.
 *  - Conditional compilation detects Linux vs Windows platform naming.
 */

#if defined(__has_include)
#if __has_include("myalloc.h")
#include "myalloc.h"
#elif __has_include("../include/myalloc.h")
#include "../include/myalloc.h"
#else
#include "myalloc.h"
#endif
#else
#include "myalloc.h"
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#if !defined(_X86_) && !defined(_AMD64_) && !defined(_ARM_) && !defined(_ARM64_)
#if defined(__i386__) || defined(_M_IX86) || defined(__MINGW32__)
#define _X86_
#elif defined(__x86_64__) || defined(_M_X64)
#define _AMD64_
#else
#define _X86_
#endif
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef LARGE_INTEGER bench_time_t;

static inline bench_time_t bench_get_time(void) {
  bench_time_t t;
  QueryPerformanceCounter(&t);
  return t;
}

static inline double get_elapsed_ms(bench_time_t start, bench_time_t end) {
  static LARGE_INTEGER freq;
  static int initialized = 0;
  if (!initialized) {
    QueryPerformanceFrequency(&freq);
    initialized = 1;
  }
  return ((double)(end.QuadPart - start.QuadPart) * 1000.0) /
         (double)freq.QuadPart;
}
#else
#include <time.h>
typedef struct timespec bench_time_t;

static inline bench_time_t bench_get_time(void) {
  bench_time_t t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t;
}

static inline double get_elapsed_ms(bench_time_t start, bench_time_t end) {
  return ((double)(end.tv_sec - start.tv_sec) * 1000.0) +
         ((double)(end.tv_nsec - start.tv_nsec) / 1000000.0);
}
#endif

#if defined(__linux__)
#define SYSTEM_MALLOC_NAME "glibc malloc"
#elif defined(_WIN32)
#define SYSTEM_MALLOC_NAME "Windows CRT malloc (MinGW-w64/UCRT)"
#else
#define SYSTEM_MALLOC_NAME "system malloc"
#endif

/* Allocator types */
typedef enum { ALLOC_FIRST_FIT, ALLOC_BEST_FIT, ALLOC_SYSTEM } AllocType;

/* Structure to store benchmark result row */
typedef struct {
  char workload[64];
  char allocator[48];
  long total_ops;
  double elapsed_ms;
  double throughput_ops_sec;
  int successful_allocs;
  int failed_allocs;
  double final_fragmentation; /* -1.0 if unmeasured */
  long largest_free_bytes;    /* -1 if unmeasured */
} BenchResult;

#define MAX_RESULTS 32
static BenchResult g_results[MAX_RESULTS];
static int g_num_results = 0;

/* Portable 32-bit xorshift PRNG */
static uint32_t g_bench_prng = 42;

static inline void bench_seed(uint32_t seed) {
  g_bench_prng = (seed == 0) ? 42 : seed;
}

static inline uint32_t bench_rand(void) {
  uint32_t x = g_bench_prng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  g_bench_prng = x;
  return x;
}

static int compare_doubles(const void *a, const void *b) {
  double da = *(const double *)a;
  double db = *(const double *)b;
  if (da < db)
    return -1;
  if (da > db)
    return 1;
  return 0;
}

static inline void *do_alloc(AllocType type, size_t size) {
  if (type == ALLOC_SYSTEM) {
    return malloc(size);
  } else {
    return my_malloc(size);
  }
}

static inline void do_free(AllocType type, void *ptr) {
  if (type == ALLOC_SYSTEM) {
    free(ptr);
  } else {
    my_free(ptr);
  }
}

/* ========================================================================= */
/* Workload Implementations                                                  */
/* ========================================================================= */

#define NUM_RUNS 5

/**
 * Workload (a): Batch small allocations + free all.
 */
static void run_workload_a(AllocType type, const char *alloc_name) {
#define WORKLOAD_A_COUNT 2500
  void *ptrs[WORKLOAD_A_COUNT];
  double times[NUM_RUNS];
  int failed = 0;

  /* 1 warm-up run + 5 timed runs */
  for (int run = -1; run < NUM_RUNS; run++) {
    if (type == ALLOC_FIRST_FIT) {
      my_set_strategy(FIRST_FIT);
      my_reset();
    } else if (type == ALLOC_BEST_FIT) {
      my_set_strategy(BEST_FIT);
      my_reset();
    }

    bench_seed(101);
    failed = 0;

    bench_time_t start = bench_get_time();

    for (int i = 0; i < WORKLOAD_A_COUNT; i++) {
      size_t sz = 64 + (bench_rand() % 64);
      ptrs[i] = do_alloc(type, sz);
      if (ptrs[i] == NULL)
        failed++;
    }

    for (int i = 0; i < WORKLOAD_A_COUNT; i++) {
      if (ptrs[i] != NULL) {
        do_free(type, ptrs[i]);
        ptrs[i] = NULL;
      }
    }

    bench_time_t end = bench_get_time();

    if (run >= 0) {
      times[run] = get_elapsed_ms(start, end);
    }
  }

  qsort(times, NUM_RUNS, sizeof(double), compare_doubles);
  double median_ms = times[NUM_RUNS / 2];
  long total_ops = WORKLOAD_A_COUNT * 2;

  BenchResult res;
  snprintf(res.workload, sizeof(res.workload), "Many Small Allocs + Free All");
  snprintf(res.allocator, sizeof(res.allocator), "%s", alloc_name);
  res.total_ops = total_ops;
  res.elapsed_ms = median_ms;
  res.throughput_ops_sec =
      (median_ms > 0.0) ? ((double)total_ops / (median_ms / 1000.0)) : 0.0;
  res.successful_allocs = WORKLOAD_A_COUNT - failed;
  res.failed_allocs = failed;

  /* Measure introspection strictly outside timed interval */
  if (type != ALLOC_SYSTEM) {
    res.final_fragmentation = my_fragmentation();
    res.largest_free_bytes = (long)my_largest_free();
  } else {
    /* External fragmentation for system allocator is unmeasured */
    res.final_fragmentation = -1.0;
    res.largest_free_bytes = -1;
  }

  g_results[g_num_results++] = res;
}

/**
 * Workload (b): Random sizes 16-4096 bytes, random alloc/free order.
 */
static void run_workload_b(AllocType type, const char *alloc_name) {
#define WORKLOAD_B_OPS 10000
#define WORKLOAD_B_SLOTS 128

  void *slots[WORKLOAD_B_SLOTS];
  double times[NUM_RUNS];
  int failed = 0;
  int success = 0;
  double last_frag = -1.0;
  long last_largest = -1;

  for (int run = -1; run < NUM_RUNS; run++) {
    if (type == ALLOC_FIRST_FIT) {
      my_set_strategy(FIRST_FIT);
      my_reset();
    } else if (type == ALLOC_BEST_FIT) {
      my_set_strategy(BEST_FIT);
      my_reset();
    }

    for (int i = 0; i < WORKLOAD_B_SLOTS; i++)
      slots[i] = NULL;
    bench_seed(202);
    failed = 0;
    success = 0;

    bench_time_t start = bench_get_time();

    for (int op = 0; op < WORKLOAD_B_OPS; op++) {
      int slot = (int)(bench_rand() % WORKLOAD_B_SLOTS);

      if (slots[slot] == NULL) {
        size_t sz = 16 + (bench_rand() % 4081);
        slots[slot] = do_alloc(type, sz);
        if (slots[slot] == NULL) {
          failed++;
        } else {
          success++;
        }
      } else {
        do_free(type, slots[slot]);
        slots[slot] = NULL;
      }
    }

    bench_time_t end = bench_get_time();

    if (run >= 0) {
      times[run] = get_elapsed_ms(start, end);
    }

    /* Measure fragmentation outside timing before clearing remaining slots */
    if (run == NUM_RUNS - 1 && type != ALLOC_SYSTEM) {
      last_frag = my_fragmentation();
      last_largest = (long)my_largest_free();
    }

    for (int i = 0; i < WORKLOAD_B_SLOTS; i++) {
      if (slots[i] != NULL) {
        do_free(type, slots[i]);
        slots[i] = NULL;
      }
    }
  }

  qsort(times, NUM_RUNS, sizeof(double), compare_doubles);
  double median_ms = times[NUM_RUNS / 2];
  long total_ops = WORKLOAD_B_OPS;

  BenchResult res;
  snprintf(res.workload, sizeof(res.workload), "Random Dynamic Churn (16-4KB)");
  snprintf(res.allocator, sizeof(res.allocator), "%s", alloc_name);
  res.total_ops = total_ops;
  res.elapsed_ms = median_ms;
  res.throughput_ops_sec =
      (median_ms > 0.0) ? ((double)total_ops / (median_ms / 1000.0)) : 0.0;
  res.successful_allocs = success;
  res.failed_allocs = failed;
  res.final_fragmentation = last_frag;
  res.largest_free_bytes = last_largest;

  g_results[g_num_results++] = res;
}

/**
 * Workload (c): Alternating pattern that deliberately causes fragmentation.
 */
static void run_workload_c(AllocType type, const char *alloc_name) {
#define WORKLOAD_C_ITEMS 300
  void *ptrs[WORKLOAD_C_ITEMS];
  void *new_ptrs[WORKLOAD_C_ITEMS / 2];
  double times[NUM_RUNS];
  int failed = 0;
  int success = 0;
  double last_frag = -1.0;
  long last_largest = -1;

  for (int run = -1; run < NUM_RUNS; run++) {
    if (type == ALLOC_FIRST_FIT) {
      my_set_strategy(FIRST_FIT);
      my_reset();
    } else if (type == ALLOC_BEST_FIT) {
      my_set_strategy(BEST_FIT);
      my_reset();
    }

    for (int i = 0; i < WORKLOAD_C_ITEMS; i++)
      ptrs[i] = NULL;
    for (int i = 0; i < WORKLOAD_C_ITEMS / 2; i++)
      new_ptrs[i] = NULL;

    bench_seed(303);
    failed = 0;
    success = 0;

    bench_time_t start = bench_get_time();

    /* 1. Allocate initial striped pattern */
    for (int i = 0; i < WORKLOAD_C_ITEMS; i++) {
      size_t sz = (i % 2 == 0) ? 128 : 256;
      ptrs[i] = do_alloc(type, sz);
      if (ptrs[i] == NULL)
        failed++;
      else
        success++;
    }

    /* 2. Free only even blocks, leaving 128B holes interspersed with active
     * blocks */
    for (int i = 0; i < WORKLOAD_C_ITEMS; i += 2) {
      if (ptrs[i] != NULL) {
        do_free(type, ptrs[i]);
        ptrs[i] = NULL;
      }
    }

    /* 3. Try to allocate 384-byte blocks (too big for 128B holes) */
    for (int i = 0; i < WORKLOAD_C_ITEMS / 2; i++) {
      new_ptrs[i] = do_alloc(type, 384);
      if (new_ptrs[i] == NULL) {
        failed++;
      } else {
        success++;
      }
    }

    bench_time_t end = bench_get_time();

    if (run >= 0) {
      times[run] = get_elapsed_ms(start, end);
    }

    if (run == NUM_RUNS - 1 && type != ALLOC_SYSTEM) {
      last_frag = my_fragmentation();
      last_largest = (long)my_largest_free();
    }

    /* Clean up all active memory */
    for (int i = 0; i < WORKLOAD_C_ITEMS; i++) {
      if (ptrs[i] != NULL) {
        do_free(type, ptrs[i]);
        ptrs[i] = NULL;
      }
    }
    for (int i = 0; i < WORKLOAD_C_ITEMS / 2; i++) {
      if (new_ptrs[i] != NULL) {
        do_free(type, new_ptrs[i]);
        new_ptrs[i] = NULL;
      }
    }
  }

  qsort(times, NUM_RUNS, sizeof(double), compare_doubles);
  double median_ms = times[NUM_RUNS / 2];
  long total_ops =
      WORKLOAD_C_ITEMS + (WORKLOAD_C_ITEMS / 2) + (WORKLOAD_C_ITEMS / 2);

  BenchResult res;
  snprintf(res.workload, sizeof(res.workload), "Adversarial Checkerboard Frag");
  snprintf(res.allocator, sizeof(res.allocator), "%s", alloc_name);
  res.total_ops = total_ops;
  res.elapsed_ms = median_ms;
  res.throughput_ops_sec =
      (median_ms > 0.0) ? ((double)total_ops / (median_ms / 1000.0)) : 0.0;
  res.successful_allocs = success;
  res.failed_allocs = failed;
  res.final_fragmentation = last_frag;
  res.largest_free_bytes = last_largest;

  g_results[g_num_results++] = res;
}

/**
 * Workload (d): Heap Saturation / Fill-until-Failure.
 * Pre-fragments heap with varied holes, then allocates larger requests until
 * exhaustion.
 */
static void run_workload_d(AllocType type, const char *alloc_name) {
#define MAX_PRE_BLOCKS 2500
#define MAX_FILL_BLOCKS 3000

  void *pre_ptrs[MAX_PRE_BLOCKS];
  void *fill_ptrs[MAX_FILL_BLOCKS];
  double times[NUM_RUNS];
  int successes = 0;
  int failures = 0;
  long total_ops = 0;
  double last_frag = -1.0;
  long last_largest = -1;

  for (int run = -1; run < NUM_RUNS; run++) {
    if (type == ALLOC_FIRST_FIT) {
      my_set_strategy(FIRST_FIT);
      my_reset();
    } else if (type == ALLOC_BEST_FIT) {
      my_set_strategy(BEST_FIT);
      my_reset();
    }

    for (int i = 0; i < MAX_PRE_BLOCKS; i++)
      pre_ptrs[i] = NULL;
    for (int i = 0; i < MAX_FILL_BLOCKS; i++)
      fill_ptrs[i] = NULL;

    bench_seed(404);
    successes = 0;
    failures = 0;
    int pre_count = 0;

    bench_time_t start = bench_get_time();

    /* Phase 1: Allocate mixed sizes (128B, 256B, 512B) up to ~800 KB */
    size_t total_pre_bytes = 0;
    while (pre_count < MAX_PRE_BLOCKS && total_pre_bytes < 800000) {
      size_t sz = 128 * (1 + (bench_rand() % 4)); /* 128, 256, 384, 512 */
      void *p = do_alloc(type, sz);
      if (p == NULL)
        break;
      pre_ptrs[pre_count++] = p;
      total_pre_bytes += sz;
    }

    /* Phase 2: Free 40% of blocks at random, creating mixed-sized holes */
    for (int i = 0; i < pre_count; i++) {
      if ((bench_rand() % 100) < 40) {
        do_free(type, pre_ptrs[i]);
        pre_ptrs[i] = NULL;
      }
    }

    /* Phase 3: Allocate larger requests (384B - 768B) until failure stop rule
     */
    int fill_count = 0;
    int consecutive_failures = 0;
    for (int i = 0; i < MAX_FILL_BLOCKS; i++) {
      size_t sz = 384 + (bench_rand() % 384);
      void *p = do_alloc(type, sz);
      if (p == NULL) {
        failures++;
        consecutive_failures++;
        /* Stop after 5 consecutive failures when incoming requests cannot be
         * serviced */
        if (type != ALLOC_SYSTEM && consecutive_failures >= 5)
          break;
        if (type == ALLOC_SYSTEM && i > 1500)
          break;
      } else {
        fill_ptrs[fill_count++] = p;
        successes++;
        consecutive_failures = 0;
      }
    }

    bench_time_t end = bench_get_time();

    if (run >= 0) {
      times[run] = get_elapsed_ms(start, end);
    }

    if (run == NUM_RUNS - 1 && type != ALLOC_SYSTEM) {
      last_frag = my_fragmentation();
      last_largest = (long)my_largest_free();
    }

    /* Cleanup */
    for (int i = 0; i < pre_count; i++) {
      if (pre_ptrs[i] != NULL) {
        do_free(type, pre_ptrs[i]);
        pre_ptrs[i] = NULL;
      }
    }
    for (int i = 0; i < fill_count; i++) {
      if (fill_ptrs[i] != NULL) {
        do_free(type, fill_ptrs[i]);
        fill_ptrs[i] = NULL;
      }
    }

    total_ops = pre_count + fill_count + failures;
  }

  qsort(times, NUM_RUNS, sizeof(double), compare_doubles);
  double median_ms = times[NUM_RUNS / 2];

  BenchResult res;
  snprintf(res.workload, sizeof(res.workload),
           "Fill-until-Failure (Saturation)");
  snprintf(res.allocator, sizeof(res.allocator), "%s", alloc_name);
  res.total_ops = total_ops;
  res.elapsed_ms = median_ms;
  res.throughput_ops_sec =
      (median_ms > 0.0) ? ((double)total_ops / (median_ms / 1000.0)) : 0.0;
  res.successful_allocs = successes;
  res.failed_allocs = failures;
  res.final_fragmentation = last_frag;
  res.largest_free_bytes = last_largest;

  g_results[g_num_results++] = res;
}

/* ========================================================================= */
/* Reporting and CSV Export                                                  */
/* ========================================================================= */

static void print_results_table(void) {
  printf("\n==================================================================="
         "==============================================================\n");
  printf("                                         BENCHMARK PERFORMANCE "
         "EVALUATION RESULTS (MEDIAN OF 5 RUNS)                            \n");
  printf("====================================================================="
         "============================================================\n");
  printf("%-33s %-25s %-7s %-10s %-15s %-8s %-8s %-12s %-14s\n", "Workload",
         "Allocator", "Ops", "Time (ms)", "Throughput(ops/s)", "Success",
         "Failed", "Frag Ratio", "Largest Free");
  printf("---------------------------------------------------------------------"
         "------------------------------------------------------------\n");

  for (int i = 0; i < g_num_results; i++) {
    BenchResult *r = &g_results[i];
    char frag_str[16];
    char largest_str[16];

    if (r->final_fragmentation >= 0.0) {
      snprintf(frag_str, sizeof(frag_str), "%.4f", r->final_fragmentation);
      snprintf(largest_str, sizeof(largest_str), "%ld B",
               r->largest_free_bytes);
    } else {
      snprintf(frag_str, sizeof(frag_str), "N/A");
      snprintf(largest_str, sizeof(largest_str), "N/A");
    }

    printf("%-33s %-25s %-7ld %-10.3f %-17.1f %-8d %-8d %-12s %-14s\n",
           r->workload, r->allocator, r->total_ops, r->elapsed_ms,
           r->throughput_ops_sec, r->successful_allocs, r->failed_allocs,
           frag_str, largest_str);
  }
  printf("====================================================================="
         "============================================================\n\n");
}

static void export_results_csv(const char *filename) {
  FILE *fp = fopen(filename, "w");
  if (!fp) {
    fprintf(stderr,
            "Warning: Unable to open '%s' for writing benchmark results\n",
            filename);
    return;
  }

  fprintf(fp, "Workload,Allocator,Ops,Elapsed_ms,Throughput_ops_sec,Success_"
              "Allocs,Failed_Allocs,Fragmentation_Ratio,Largest_Free_Bytes\n");
  for (int i = 0; i < g_num_results; i++) {
    BenchResult *r = &g_results[i];
    if (r->final_fragmentation >= 0.0) {
      fprintf(fp, "\"%s\",\"%s\",%ld,%.3f,%.1f,%d,%d,%.4f,%ld\n", r->workload,
              r->allocator, r->total_ops, r->elapsed_ms, r->throughput_ops_sec,
              r->successful_allocs, r->failed_allocs, r->final_fragmentation,
              r->largest_free_bytes);
    } else {
      fprintf(fp, "\"%s\",\"%s\",%ld,%.3f,%.1f,%d,%d,N/A,N/A\n", r->workload,
              r->allocator, r->total_ops, r->elapsed_ms, r->throughput_ops_sec,
              r->successful_allocs, r->failed_allocs);
    }
  }

  fclose(fp);
  printf("[Info] Benchmark results successfully exported to: %s\n", filename);
}

int main(void) {
  printf("Starting allocator benchmarks (5 runs per configuration, median "
         "reported)...\n");

  /* (a) Batch Small Allocations */
  printf("Running Workload (a): Batch small allocations + free all...\n");
  run_workload_a(ALLOC_FIRST_FIT, "First-Fit");
  run_workload_a(ALLOC_BEST_FIT, "Best-Fit");
  run_workload_a(ALLOC_SYSTEM, SYSTEM_MALLOC_NAME);

  /* (b) Random Dynamic Churn */
  printf("Running Workload (b): Random dynamic churn...\n");
  run_workload_b(ALLOC_FIRST_FIT, "First-Fit");
  run_workload_b(ALLOC_BEST_FIT, "Best-Fit");
  run_workload_b(ALLOC_SYSTEM, SYSTEM_MALLOC_NAME);

  /* (c) Adversarial Checkerboard Fragmentation */
  printf("Running Workload (c): Adversarial checkerboard fragmentation...\n");
  run_workload_c(ALLOC_FIRST_FIT, "First-Fit");
  run_workload_c(ALLOC_BEST_FIT, "Best-Fit");
  run_workload_c(ALLOC_SYSTEM, SYSTEM_MALLOC_NAME);

  /* (d) Heap Saturation / Fill-until-Failure */
  printf("Running Workload (d): Heap saturation / fill-until-failure...\n");
  run_workload_d(ALLOC_FIRST_FIT, "First-Fit");
  run_workload_d(ALLOC_BEST_FIT, "Best-Fit");
  run_workload_d(ALLOC_SYSTEM, SYSTEM_MALLOC_NAME);

  /* Print Table */
  print_results_table();

  /* Write CSV */
  export_results_csv("bench/results.csv");

  return 0;
}
