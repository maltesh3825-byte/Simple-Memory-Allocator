/**
 * @file myalloc.h
 * @brief Header file for a 16-byte aligned custom memory allocator (malloc/free clone).
 *
 * This allocator operates over a static 1 MB memory buffer using an address-ordered
 * doubly linked list of block headers. It supports both First-Fit and Best-Fit
 * search strategies, block splitting, and O(1) immediate boundary coalescing.
 *
 * Designed to be clean, educational, and easy to explain for first-year computer
 * systems and C programming students.
 */

#ifndef MYALLOC_H
#define MYALLOC_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Memory alignment requirement in bytes.
 * Modern 64-bit systems and vector instructions (e.g., SSE) require 16-byte
 * alignment to prevent hardware penalties or bus errors.
 */
#define ALIGNMENT 16

/**
 * @brief Total heap capacity: 1 MB (1,048,576 bytes).
 */
#define HEAP_SIZE (1024 * 1024)

/**
 * @brief Allocation search strategies.
 */
enum fit_strategy {
    FIRST_FIT, /**< Choose the first free block that is large enough */
    BEST_FIT   /**< Search all free blocks and pick the smallest one that fits */
};

/**
 * @brief Header structure prepended to every memory chunk in the heap.
 *
 * Layout:
 *   [ struct Block header (aligned) ][ User Payload (16-byte aligned) ]
 *
 * Every block in our 1 MB heap has this header. The user receives a pointer
 * to the payload immediately following the header.
 */
struct Block {
    size_t size;         /**< Usable payload size in bytes (excludes header) */
    int free;            /**< 1 if free (available), 0 if allocated (in use) */
    struct Block *next;  /**< Pointer to the physically next block in memory */
    struct Block *prev;  /**< Pointer to the physically previous block in memory */
};

_Static_assert(sizeof(struct Block) % ALIGNMENT == 0,
               "Block header must preserve payload alignment");

/* --- Core Allocator API --- */

/**
 * @brief Allocates `size` bytes of memory from the static heap.
 *
 * The returned pointer is guaranteed to be 16-byte aligned.
 *
 * @param size Number of bytes requested.
 * @return void* Pointer to usable payload, or NULL if size == 0 or insufficient memory.
 */
void *my_malloc(size_t size);

/**
 * @brief Releases allocated memory back to the heap.
 *
 * Performs boundary checks, detects double frees safely without crashing,
 * and immediately coalesces with adjacent free neighbors in O(1) time.
 *
 * @param ptr Pointer previously returned by my_malloc (or NULL).
 */
void my_free(void *ptr);

/**
 * @brief Changes the allocation search strategy (FIRST_FIT or BEST_FIT).
 *
 * @param strategy The strategy to use for subsequent allocations.
 */
void my_set_strategy(enum fit_strategy strategy);

/**
 * @brief Retrieves the currently active allocation strategy.
 *
 * @return enum fit_strategy Current strategy.
 */
enum fit_strategy my_get_strategy(void);

/**
 * @brief Resets the heap back to its initial state: one single 1 MB free block.
 *
 * Useful between benchmark runs or unit tests to start with a clean slate.
 */
void my_reset(void);

/* --- Diagnostic and Introspection API --- */

/**
 * @brief Calculates the total number of free bytes across all free blocks.
 *
 * @return size_t Sum of payload sizes of all currently free blocks.
 */
size_t my_total_free(void);

/**
 * @brief Finds the size of the single largest contiguous free block.
 *
 * @return size_t Largest free payload size in bytes (0 if no blocks are free).
 */
size_t my_largest_free(void);

/**
 * @brief Computes the external memory fragmentation ratio.
 *
 * Formula: 1.0 - ((double)largest_free / (double)total_free)
 * If total_free == 0, returns 0.0.
 *
 * A value of 0.0 means perfect contiguity (e.g., a single large free block).
 * A value close to 1.0 means severe fragmentation (lots of tiny scattered holes).
 *
 * @return double Fragmentation metric between 0.0 and 1.0.
 */
double my_fragmentation(void);

/**
 * @brief Prints an ASCII map and detailed table of all blocks currently in the heap.
 *
 * Outputs address, header address, payload size, status (USED/FREE), and list links.
 */
void my_dump_heap(void);

#endif /* MYALLOC_H */
