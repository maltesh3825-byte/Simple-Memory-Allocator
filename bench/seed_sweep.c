/**
 * @file seed_sweep.c
 * @brief Repeats benchmark workload (d) (heap saturation / fill-until-failure)
 * across many PRNG seeds to check whether Best-Fit's packing advantage over
 * First-Fit is systematic or just single-seed noise.
 *
 * Mirrors run_workload_d() in bench/bench.c exactly (same PRNG, same phases,
 * same 5-consecutive-failure stop rule); only the seed changes.
 *
 * Build/run (from project root):
 *   gcc -std=c11 -O2 -Wall -Wextra -Iinclude src/myalloc.c bench/seed_sweep.c
 * -o seed_sweep && ./seed_sweep
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

#define MAX_PRE_BLOCKS 2500
#define MAX_FILL_BLOCKS 3000
#define NUM_SEEDS 300

static uint32_t g_state = 42;
static void seed_prng(uint32_t s) { g_state = (s == 0) ? 42 : s; }
static uint32_t next_rand(void) {
  uint32_t x = g_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  g_state = x;
  return x;
}

/* Returns number of successful fill allocations for one seed/strategy. */
static int run_saturation(enum fit_strategy strat, uint32_t seed,
                          int *failures_out) {
  void *pre[MAX_PRE_BLOCKS];
  void *fill[MAX_FILL_BLOCKS];
  int pre_count = 0, fill_count = 0, failures = 0, consecutive = 0;
  size_t pre_bytes = 0;

  my_set_strategy(strat);
  my_reset();
  seed_prng(seed);

  while (pre_count < MAX_PRE_BLOCKS && pre_bytes < 800000) {
    size_t sz = 128 * (1 + (next_rand() % 4));
    void *p = my_malloc(sz);
    if (p == NULL)
      break;
    pre[pre_count++] = p;
    pre_bytes += sz;
  }
  for (int i = 0; i < pre_count; i++) {
    if ((next_rand() % 100) < 40) {
      my_free(pre[i]);
      pre[i] = NULL;
    }
  }
  for (int i = 0; i < MAX_FILL_BLOCKS; i++) {
    size_t sz = 384 + (next_rand() % 384);
    void *p = my_malloc(sz);
    if (p == NULL) {
      failures++;
      if (++consecutive >= 5)
        break;
    } else {
      fill[fill_count++] = p;
      consecutive = 0;
    }
  }
  for (int i = 0; i < pre_count; i++)
    if (pre[i])
      my_free(pre[i]);
  for (int i = 0; i < fill_count; i++)
    if (fill[i])
      my_free(fill[i]);

  *failures_out = failures;
  return fill_count;
}

int main(void) {
  int bf_wins = 0, ff_wins = 0, ties = 0, f;
  long sum_diff = 0, sum_ff = 0;
  int min_diff = 1 << 30, max_diff = -(1 << 30);

  /* Sanity check: the benchmark's own seed (404) must match results.csv */
  int ff404 = run_saturation(FIRST_FIT, 404, &f);
  int ff404_f = f;
  int bf404 = run_saturation(BEST_FIT, 404, &f);
  printf("Seed 404 (bench.c): First-Fit %d successes / %d failures, Best-Fit "
         "%d successes / %d failures\n",
         ff404, ff404_f, bf404, f);

  for (uint32_t s = 1; s <= NUM_SEEDS; s++) {
    int ff = run_saturation(FIRST_FIT, s, &f);
    int bf = run_saturation(BEST_FIT, s, &f);
    int d = bf - ff;
    sum_diff += d;
    sum_ff += ff;
    if (d > 0)
      bf_wins++;
    else if (d < 0)
      ff_wins++;
    else
      ties++;
    if (d < min_diff)
      min_diff = d;
    if (d > max_diff)
      max_diff = d;
  }
  printf(
      "Seeds 1..%d: Best-Fit more successes in %d, First-Fit in %d, ties %d\n",
      NUM_SEEDS, bf_wins, ff_wins, ties);
  printf("Mean advantage (Best-Fit minus First-Fit): %.2f allocations (%.2f%% "
         "of First-Fit's mean %.1f); range [%d, %d]\n",
         (double)sum_diff / NUM_SEEDS,
         100.0 * (double)sum_diff / (double)sum_ff, (double)sum_ff / NUM_SEEDS,
         min_diff, max_diff);
  return 0;
}
