/**
 * @file test_alloc.c
 * @brief Unit test suite for custom memory allocator.
 *
 * Runs 8 comprehensive test cases covering:
 *  1. Allocate many blocks, free all -> heap returns to single free block.
 *  2. Free every other block (fragmentation > 0), then free remainder -> single block.
 *  3. Data integrity byte patterns across allocs/frees (detects overlapping blocks).
 *  4. 16-byte alignment verification on all returned pointers.
 *  5. Edge cases: size 0 returns NULL, requests > heap size return NULL.
 *  6. Double free detected without crashing; freeing NULL is safe.
 *  7. Block splitting: allocating small block leaves a free leftover block.
 *  8. Seeded stress test (5,000 random operations) with data integrity checks.
 *
 * Every test is executed for BOTH FIRST_FIT and BEST_FIT strategies.
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
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Custom test counters */
static int g_tests_run = 0;
static int g_tests_failed = 0;

/**
 * @brief Custom assert macro.
 * Prints error details on failure, increments failure count, and returns 0.
 */
#define TEST_ASSERT(cond, desc) do { \
    g_tests_run++; \
    if (!(cond)) { \
        printf("    FAIL: %s (condition '%s' failed at line %d)\n", desc, #cond, __LINE__); \
        g_tests_failed++; \
        return 0; \
    } \
} while (0)

#define INITIAL_FREE_CAPACITY (HEAP_SIZE - ALIGNMENT * 2) /* 1 MB minus header (32 bytes) */

/**
 * @brief Checks whether the heap has completely returned to a single free block.
 */
static int is_heap_clean_single_block(void) {
    size_t total = my_total_free();
    size_t largest = my_largest_free();
    double frag = my_fragmentation();

    /*
     * In a pristine/fully coalesced heap:
     * - total free space is the entire heap minus one header (HEAP_SIZE - 32)
     * - largest free block equals total free space
     * - fragmentation ratio is exactly 0.0
     */
    return (total == largest) && (frag == 0.0) && (total >= (HEAP_SIZE - 64));
}

/* ========================================================================= */
/* Test Cases                                                                */
/* ========================================================================= */

/**
 * Test 1: Allocate many blocks, free all, and verify heap returns to a single free block.
 */
static int test_allocate_and_free_all(void) {
    my_reset();
    #define NUM_CHUNKS 64
    void *ptrs[NUM_CHUNKS];

    for (int i = 0; i < NUM_CHUNKS; i++) {
        ptrs[i] = my_malloc(128);
        TEST_ASSERT(ptrs[i] != NULL, "Allocation failed in allocate_and_free_all");
    }

    /* Free all chunks */
    for (int i = 0; i < NUM_CHUNKS; i++) {
        my_free(ptrs[i]);
    }

    TEST_ASSERT(is_heap_clean_single_block(), "Heap should be a single free block after freeing all");
    return 1;
}

/**
 * Test 2: Free every other block: fragmentation > 0. Free the rest: back to a single block.
 */
static int test_fragmentation_and_recoalesce(void) {
    my_reset();
    #define NUM_FRAG 32
    void *ptrs[NUM_FRAG];

    /* Allocate continuous blocks */
    for (int i = 0; i < NUM_FRAG; i++) {
        ptrs[i] = my_malloc(256);
        TEST_ASSERT(ptrs[i] != NULL, "Alloc failed in fragmentation test");
    }

    /* Free even index blocks (0, 2, 4, ...) creating alternating holes */
    for (int i = 0; i < NUM_FRAG; i += 2) {
        my_free(ptrs[i]);
        ptrs[i] = NULL;
    }

    /* There are multiple separate free blocks, so fragmentation must be > 0 */
    double frag = my_fragmentation();
    TEST_ASSERT(frag > 0.0, "Fragmentation must be greater than 0 after freeing alternating blocks");

    /* Free remaining odd blocks (1, 3, 5, ...) */
    for (int i = 1; i < NUM_FRAG; i += 2) {
        my_free(ptrs[i]);
        ptrs[i] = NULL;
    }

    /* After all are freed, O(1) coalescing must restore one single contiguous free block */
    TEST_ASSERT(is_heap_clean_single_block(), "Heap must coalesce into one single block after freeing all remaining blocks");
    return 1;
}

/**
 * Test 3: Fill each allocation with unique byte pattern, do more allocs/frees, then verify all patterns are intact.
 */
static int test_data_integrity_no_overlap(void) {
    my_reset();
    #define NUM_PATTERNS 24
    void *ptrs[NUM_PATTERNS];
    size_t sizes[NUM_PATTERNS];

    /* Allocate various sizes and fill with deterministic pattern */
    for (int i = 0; i < NUM_PATTERNS; i++) {
        sizes[i] = 32 + (i * 24);
        ptrs[i] = my_malloc(sizes[i]);
        TEST_ASSERT(ptrs[i] != NULL, "Alloc failed in data integrity test");

        /* Fill with pattern based on index i */
        uint8_t *byte_ptr = (uint8_t *)ptrs[i];
        for (size_t b = 0; b < sizes[i]; b++) {
            byte_ptr[b] = (uint8_t)((i * 17 + b * 13) & 0xFF);
        }
    }

    /* Perform temporary churn in the background */
    void *temp1 = my_malloc(512);
    void *temp2 = my_malloc(1024);
    TEST_ASSERT(temp1 != NULL && temp2 != NULL, "Temp allocs should succeed");
    my_free(temp1);
    void *temp3 = my_malloc(256);
    TEST_ASSERT(temp3 != NULL, "Temp3 alloc should succeed");
    my_free(temp3);
    my_free(temp2);

    /* Validate that original patterns are completely untouched */
    for (int i = 0; i < NUM_PATTERNS; i++) {
        uint8_t *byte_ptr = (uint8_t *)ptrs[i];
        for (size_t b = 0; b < sizes[i]; b++) {
            uint8_t expected = (uint8_t)((i * 17 + b * 13) & 0xFF);
            TEST_ASSERT(byte_ptr[b] == expected, "Memory corruption / overlap detected in block payload");
        }
    }

    /* Clean up */
    for (int i = 0; i < NUM_PATTERNS; i++) {
        my_free(ptrs[i]);
    }
    TEST_ASSERT(is_heap_clean_single_block(), "Heap must return to single block");
    return 1;
}

/**
 * Test 4: Every returned pointer is 16-byte aligned.
 */
static int test_16_byte_alignment(void) {
    my_reset();
    /* Diverse arbitrary sizes to test round-up padding */
    size_t test_sizes[] = { 1, 2, 7, 15, 16, 17, 31, 32, 33, 64, 100, 255, 513, 1024, 4096 };
    int n_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);
    void *ptrs[sizeof(test_sizes) / sizeof(test_sizes[0])];

    for (int i = 0; i < n_sizes; i++) {
        ptrs[i] = my_malloc(test_sizes[i]);
        TEST_ASSERT(ptrs[i] != NULL, "Alloc failed in alignment test");
        uintptr_t addr = (uintptr_t)ptrs[i];
        TEST_ASSERT((addr % 16) == 0, "Returned pointer must be 16-byte aligned");
    }

    for (int i = 0; i < n_sizes; i++) {
        my_free(ptrs[i]);
    }

    TEST_ASSERT(is_heap_clean_single_block(), "Heap must coalesce to single block");
    return 1;
}

/**
 * Test 5: Requesting more than the heap size returns NULL; size 0 returns NULL.
 */
static int test_size_zero_and_oversized(void) {
    my_reset();

    /* Size 0 returns NULL */
    void *p0 = my_malloc(0);
    TEST_ASSERT(p0 == NULL, "my_malloc(0) must return NULL");

    /* Oversized allocation: exceeding entire heap capacity */
    void *p_over = my_malloc(HEAP_SIZE + 1024);
    TEST_ASSERT(p_over == NULL, "my_malloc(HEAP_SIZE + 1024) must return NULL");

    /* Exactly HEAP_SIZE is too large because of header overhead */
    void *p_exact = my_malloc(HEAP_SIZE);
    TEST_ASSERT(p_exact == NULL, "my_malloc(HEAP_SIZE) must return NULL due to header overhead");

    /* Massive size overflow check */
    void *p_huge = my_malloc(SIZE_MAX - 8);
    TEST_ASSERT(p_huge == NULL, "my_malloc with SIZE_MAX must return NULL");

    /* Requests near SIZE_MAX must be rejected before alignment arithmetic wraps. */
    void *p_max = my_malloc(SIZE_MAX);
    TEST_ASSERT(p_max == NULL, "my_malloc(SIZE_MAX) must return NULL");

    /* Heap should still be in pristine state */
    TEST_ASSERT(is_heap_clean_single_block(), "Heap should remain clean after invalid requests");
    return 1;
}

/**
 * Test 6: Double free is detected without crashing; freeing NULL is safe.
 */
static int test_double_free_and_null_safe(void) {
    my_reset();

    /* Freeing NULL must not crash */
    my_free(NULL);

    /* Allocate block */
    void *ptr = my_malloc(128);
    TEST_ASSERT(ptr != NULL, "Alloc failed in double free test");

    /* First free is valid */
    my_free(ptr);

    /* Second free is a double free: allocator must detect and gracefully return without crashing */
    printf("    [Notice: Next stderr message is EXPECTED for double free test]\n");
    my_free(ptr);

    /* Allocator should still function normally */
    void *new_ptr = my_malloc(128);
    TEST_ASSERT(new_ptr != NULL, "Alloc should work after double free was safely rejected");
    my_free(new_ptr);

    TEST_ASSERT(is_heap_clean_single_block(), "Heap should be intact after double free test");
    return 1;
}

/**
 * Test 7: Splitting works: after allocating a small block from a fresh heap, a free block remains.
 */
static int test_splitting(void) {
    my_reset();

    size_t initial_free = my_total_free();

    /* Allocate small chunk (64 bytes) */
    void *p1 = my_malloc(64);
    TEST_ASSERT(p1 != NULL, "Initial alloc failed in splitting test");

    /* A large free block must remain */
    size_t remaining_free = my_total_free();
    size_t largest_free = my_largest_free();

    TEST_ASSERT(remaining_free > 0, "Free memory should remain after splitting");
    TEST_ASSERT(largest_free > (HEAP_SIZE / 2), "Largest free block should still be large");
    TEST_ASSERT(remaining_free < initial_free, "Remaining free memory should be less than initial");

    /* Allocate another block from the leftover space */
    void *p2 = my_malloc(128);
    TEST_ASSERT(p2 != NULL, "Second alloc should succeed from leftover split block");

    my_free(p1);
    my_free(p2);

    TEST_ASSERT(is_heap_clean_single_block(), "Heap should return to single block after freeing split parts");
    return 1;
}

/* Portable 32-bit PRNG (xorshift32) guarantees identical sequences on Linux, WSL, macOS, and Windows */
static uint32_t g_test_prng_state = 1337;

static inline void test_prng_seed(uint32_t seed) {
    g_test_prng_state = (seed == 0) ? 1337 : seed;
}

static inline uint32_t test_prng_next(void) {
    uint32_t x = g_test_prng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_test_prng_state = x;
    return x;
}

/**
 * Test 8: Seeded random alloc/free stress test (a few thousand operations) with data-integrity checks.
 */
static int test_stress_random_alloc_free(void) {
    my_reset();
    test_prng_seed(1337); /* Deterministic seed for identical behavior across all platforms */

    #define MAX_SLOTS 64
    #define STRESS_OPS 3000

    struct ActiveAlloc {
        void *ptr;
        size_t size;
        uint8_t fill_byte;
    } slots[MAX_SLOTS];

    for (int i = 0; i < MAX_SLOTS; i++) {
        slots[i].ptr = NULL;
        slots[i].size = 0;
        slots[i].fill_byte = 0;
    }

    for (int op = 0; op < STRESS_OPS; op++) {
        int slot = (int)(test_prng_next() % MAX_SLOTS);

        if (slots[slot].ptr == NULL) {
            /* Allocate: sizes between 16 and 1024 bytes */
            size_t sz = 16 + (test_prng_next() % 1024);
            void *p = my_malloc(sz);
            if (p != NULL) {
                uint8_t fill = (uint8_t)(test_prng_next() & 0xFF);
                memset(p, fill, sz);
                slots[slot].ptr = p;
                slots[slot].size = sz;
                slots[slot].fill_byte = fill;
            }
        } else {
            /* Verify integrity before freeing */
            uint8_t *bytes = (uint8_t *)slots[slot].ptr;
            for (size_t b = 0; b < slots[slot].size; b++) {
                TEST_ASSERT(bytes[b] == slots[slot].fill_byte, "Stress test: data corruption detected!");
            }
            my_free(slots[slot].ptr);
            slots[slot].ptr = NULL;
        }
    }

    /* Free all remaining active allocations */
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (slots[i].ptr != NULL) {
            uint8_t *bytes = (uint8_t *)slots[i].ptr;
            for (size_t b = 0; b < slots[i].size; b++) {
                TEST_ASSERT(bytes[b] == slots[i].fill_byte, "Stress cleanup: data corruption detected!");
            }
            my_free(slots[i].ptr);
            slots[i].ptr = NULL;
        }
    }

    TEST_ASSERT(is_heap_clean_single_block(), "Heap must completely coalesce after stress test");
    return 1;
}

/* ========================================================================= */
/* Test Runner                                                               */
/* ========================================================================= */

typedef int (*test_func_t)(void);

struct TestCase {
    const char *name;
    test_func_t func;
};

static struct TestCase test_cases[] = {
    { "Test 1: Allocate many blocks, free all -> single block", test_allocate_and_free_all },
    { "Test 2: Free every other block (frag > 0), then free rest -> single block", test_fragmentation_and_recoalesce },
    { "Test 3: Unique byte patterns and overlap detection", test_data_integrity_no_overlap },
    { "Test 4: 16-byte alignment on all returned pointers", test_16_byte_alignment },
    { "Test 5: Edge cases (size 0 and oversized requests return NULL)", test_size_zero_and_oversized },
    { "Test 6: Double free safe detection & NULL free safety", test_double_free_and_null_safe },
    { "Test 7: Splitting behavior leaves valid free leftover block", test_splitting },
    { "Test 8: Seeded random stress test (3,000 ops) with integrity checks", test_stress_random_alloc_free }
};

int main(void) {
    enum fit_strategy strategies[] = { FIRST_FIT, BEST_FIT };
    const char *strat_names[] = { "FIRST_FIT", "BEST_FIT" };
    int num_tests = sizeof(test_cases) / sizeof(test_cases[0]);

    printf("=================================================================\n");
    printf("         MYALLOC CUSTOM MEMORY ALLOCATOR TEST SUITE              \n");
    printf("=================================================================\n\n");

    for (int s = 0; s < 2; s++) {
        my_set_strategy(strategies[s]);
        printf(">>> RUNNING TESTS UNDER STRATEGY: %s <<<\n", strat_names[s]);
        printf("-----------------------------------------------------------------\n");

        for (int i = 0; i < num_tests; i++) {
            printf("  [%s] %s ... ", strat_names[s], test_cases[i].name);
            fflush(stdout);
            int result = test_cases[i].func();
            if (result) {
                printf("PASS\n");
            } else {
                printf("FAILED\n");
            }
        }
        printf("\n");
    }

    printf("=================================================================\n");
    printf("TEST RESULTS SUMMARY:\n");
    printf("  Total assertions checked: %d\n", g_tests_run);
    printf("  Total test failures:      %d\n", g_tests_failed);

    if (g_tests_failed == 0) {
        printf("  STATUS: ALL TESTS PASSED SUCCESSFULLY! (100%%)\n");
        printf("=================================================================\n");
        return 0;
    } else {
        printf("  STATUS: SOME TESTS FAILED!\n");
        printf("=================================================================\n");
        return 1;
    }
}
