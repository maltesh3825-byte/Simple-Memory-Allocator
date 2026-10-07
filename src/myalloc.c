/**
 * @file myalloc.c
 * @brief Implementation of a custom memory allocator (malloc/free clone).
 *
 * Designed for first-year computer systems students to understand:
 *  1. How dynamic memory allocation works under the hood without calling sbrk/mmap.
 *  2. Pointer arithmetic and memory alignment (16-byte alignment).
 *  3. Explicit metadata headers attached to every memory chunk.
 *  4. Doubly linked lists maintained in strict physical address order.
 *  5. Splitting large blocks to minimize internal fragmentation.
 *  6. O(1) immediate boundary coalescing to eliminate external fragmentation.
 *  7. Search heuristics: First-Fit vs. Best-Fit.
 */

#include "myalloc.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================= */
/* Macros and Constants                                                      */
/* ========================================================================= */

/**
 * Helper macro to round up a value `x` to the next multiple of ALIGNMENT (16).
 *
 * How this bitwise trick works:
 * Adding (ALIGNMENT - 1) forces any value not already a multiple into the next multiple's range.
 * Bitwise AND with ~(ALIGNMENT - 1) clears the lower 4 bits (since 16 is 0b10000),
 * effectively rounding down to the nearest multiple of 16.
 */
#define ALIGN_SIZE(x) (((x) + (ALIGNMENT - 1)) & ~((size_t)(ALIGNMENT - 1)))

/**
 * Size of the block header rounded up to 16 bytes.
 * On 64-bit systems, sizeof(struct Block) is 32 bytes (size_t 8B + int 4B + 4B pad + two 8B ptrs).
 * 32 bytes is already a multiple of 16, but ALIGN_SIZE ensures safety on all platforms.
 */
#define BLOCK_HEADER_SIZE ALIGN_SIZE(sizeof(struct Block))

/**
 * Minimum leftover payload size required to split a block.
 * When splitting a free block, the leftover piece must be able to hold:
 *   [ struct Block header ] + [ at least 16 bytes of usable payload ]
 */
#define MIN_SPLIT_PAYLOAD ALIGNMENT
#define MIN_SPLIT_TOTAL (BLOCK_HEADER_SIZE + MIN_SPLIT_PAYLOAD)

/* ========================================================================= */
/* Static Heap Storage                                                       */
/* ========================================================================= */

/**
 * The static 1 MB heap buffer.
 *
 * - `_Alignas(16)`: Guarantees that the base address of the heap array starts on
 *   a 16-byte aligned boundary in memory.
 * - Because the heap starts 16-byte aligned, and BLOCK_HEADER_SIZE is a multiple
 *   of 16, the very first payload returned will also be 16-byte aligned.
 */
static _Alignas(16) uint8_t heap[HEAP_SIZE];

/**
 * Head pointer of our doubly-linked list.
 * Blocks in this list are kept in STRICT physical address order from lowest to highest.
 */
static struct Block *heap_head = NULL;

/**
 * Flag indicating whether the heap has been initialized.
 * 0 = uninitialized, 1 = initialized.
 */
static int heap_initialized = 0;

/**
 * Active search strategy for finding free blocks.
 * Defaults to FIRST_FIT.
 */
static enum fit_strategy current_strategy = FIRST_FIT;

/* ========================================================================= */
/* Internal Helper Functions                                                 */
/* ========================================================================= */

/**
 * @brief Converts a user payload pointer to its corresponding Block header pointer.
 *
 * In memory, the header immediately precedes the payload:
 * [ Block Header (BLOCK_HEADER_SIZE) ][ Payload ... ]
 *                                      ^
 *                                     ptr
 */
static inline struct Block *payload_to_block(void *ptr) {
    return (struct Block *)((uint8_t *)ptr - BLOCK_HEADER_SIZE);
}

/**
 * @brief Converts a Block header pointer to its usable payload pointer.
 */
static inline void *block_to_payload(struct Block *block) {
    return (void *)((uint8_t *)block + BLOCK_HEADER_SIZE);
}

/**
 * @brief Initializes the heap as a single giant free block spanning all 1 MB.
 *
 * Called lazily on the first invocation of my_malloc.
 */
static void my_init(void) {
    heap_head = (struct Block *)heap;
    heap_head->size = HEAP_SIZE - BLOCK_HEADER_SIZE;
    heap_head->free = 1;
    heap_head->next = NULL;
    heap_head->prev = NULL;
    heap_initialized = 1;
}

/**
 * @brief Searches the free list for a block that satisfies the requested size.
 *
 * Supports two strategies:
 *  - FIRST_FIT: Returns the first free block with size >= req_size.
 *  - BEST_FIT: Scans the entire list to find the free block with the smallest size >= req_size.
 *
 * @param req_size Aligned payload size requested.
 * @return struct Block* Pointer to the chosen block, or NULL if no block fits.
 */
static struct Block *find_fit(size_t req_size) {
    struct Block *curr = heap_head;

    if (current_strategy == FIRST_FIT) {
        /* FIRST_FIT: Stop at the very first block that is free and large enough */
        while (curr != NULL) {
            if (curr->free && curr->size >= req_size) {
                return curr;
            }
            curr = curr->next;
        }
        return NULL;
    } else {
        /* BEST_FIT: Exhaustively scan all blocks and keep the closest fit */
        struct Block *best = NULL;
        while (curr != NULL) {
            if (curr->free && curr->size >= req_size) {
                if (best == NULL || curr->size < best->size) {
                    best = curr;
                    /* If we find an exact match, we cannot do better than this */
                    if (best->size == req_size) {
                        return best;
                    }
                }
            }
            curr = curr->next;
        }
        return best;
    }
}

/**
 * @brief Splits a free block into an allocated block and a leftover free block.
 *
 * Only splits if the leftover space is large enough to hold a new Block header
 * plus at least MIN_SPLIT_PAYLOAD (16 bytes) of payload.
 *
 * @param block The block to split.
 * @param req_size The exact aligned payload size needed for the allocation.
 */
static void split_block(struct Block *block, size_t req_size) {
    /* Calculate remaining space after carving out req_size and a new header */
    if (block->size >= req_size + MIN_SPLIT_TOTAL) {
        size_t leftover_payload = block->size - req_size - BLOCK_HEADER_SIZE;

        /* Calculate address of new block: exactly req_size bytes after current payload */
        struct Block *new_block = (struct Block *)((uint8_t *)block_to_payload(block) + req_size);

        /* Configure metadata for the leftover block */
        new_block->size = leftover_payload;
        new_block->free = 1;
        new_block->next = block->next;
        new_block->prev = block;

        /* Update pointers in the doubly linked list */
        if (block->next != NULL) {
            block->next->prev = new_block;
        }
        block->next = new_block;

        /* Shrink the allocated block's size */
        block->size = req_size;
    }
}

/**
 * @brief Coalesces a newly freed block with its immediate physical neighbors.
 *
 * Because blocks are linked in strict physical address order:
 *  - block->prev is physically adjacent to block on the left (lower address)
 *  - block->next is physically adjacent to block on the right (higher address)
 *
 * This allows O(1) merging without scanning any other part of the list!
 *
 * @param block The block that was just freed.
 * @return struct Block* The resulting merged block.
 */
static struct Block *coalesce_block(struct Block *block) {
    /* Case 1: Merge with physically NEXT block if it is free */
    if (block->next != NULL && block->next->free) {
        struct Block *next_block = block->next;
        /* Absorb the next block's header and payload */
        block->size += BLOCK_HEADER_SIZE + next_block->size;
        block->next = next_block->next;
        if (block->next != NULL) {
            block->next->prev = block;
        }
    }

    /* Case 2: Merge with physically PREVIOUS block if it is free */
    if (block->prev != NULL && block->prev->free) {
        struct Block *prev_block = block->prev;
        /* Absorb the current block's header and payload into prev */
        prev_block->size += BLOCK_HEADER_SIZE + block->size;
        prev_block->next = block->next;
        if (block->next != NULL) {
            block->next->prev = prev_block;
        }
        block = prev_block;
    }

    return block;
}

/* ========================================================================= */
/* Public API Implementation                                                 */
/* ========================================================================= */

void my_set_strategy(enum fit_strategy strategy) {
    current_strategy = strategy;
}

enum fit_strategy my_get_strategy(void) {
    return current_strategy;
}

void *my_malloc(size_t size) {
    /* 1. Request for 0 bytes returns NULL according to specification */
    if (size == 0) {
        return NULL;
    }

    /* 2. Lazy initialization: set up the 1 MB heap on the first allocation */
    if (!heap_initialized) {
        my_init();
    }

    /* 3. Round requested size up to the nearest multiple of 16 */
    size_t req_size = ALIGN_SIZE(size);

    /* Guard against integer overflow or requests exceeding entire heap capacity */
    if (req_size < size || req_size > (HEAP_SIZE - BLOCK_HEADER_SIZE)) {
        return NULL;
    }

    /* 4. Find a suitable free block using current strategy */
    struct Block *block = find_fit(req_size);
    if (block == NULL) {
        /* No free block large enough (heap is out of memory or fragmented) */
        return NULL;
    }

    /* 5. Split block if leftover space is large enough */
    split_block(block, req_size);

    /* 6. Mark block as used */
    block->free = 0;

    /* 7. Return pointer to the aligned payload */
    return block_to_payload(block);
}

void my_free(void *ptr) {
    /* 1. Freeing NULL is explicitly a safe no-op */
    if (ptr == NULL) {
        return;
    }

    /* 2. Boundary and alignment validation */
    uintptr_t uptr = (uintptr_t)ptr;
    uintptr_t heap_start = (uintptr_t)heap;
    uintptr_t heap_end = heap_start + HEAP_SIZE;

    /* Verify payload pointer alignment */
    if ((uptr % ALIGNMENT) != 0) {
        fprintf(stderr, "my_free: pointer %p is not 16-byte aligned\n", ptr);
        return;
    }

    /* Check that payload pointer lies strictly inside valid heap payload range */
    if (uptr < (heap_start + BLOCK_HEADER_SIZE) || uptr >= heap_end) {
        fprintf(stderr, "my_free: pointer %p lies outside static heap range [%p, %p)\n",
                ptr, (void *)(heap_start + BLOCK_HEADER_SIZE), (void *)heap_end);
        return;
    }

    /* Retrieve header */
    struct Block *block = payload_to_block(ptr);
    uintptr_t b_addr = (uintptr_t)block;

    /* Header sanity checks */
    if (b_addr < heap_start || (b_addr + BLOCK_HEADER_SIZE + block->size) > heap_end) {
        fprintf(stderr, "my_free: corrupted block header at %p\n", (void *)block);
        return;
    }

    /* 3. Detect Double Free */
    if (block->free == 1) {
        fprintf(stderr, "my_free: double free detected at %p (block %p)\n", ptr, (void *)block);
        return;
    }

    /* 4. Mark block free */
    block->free = 1;

    /* 5. Immediate O(1) coalescing with neighbors */
    coalesce_block(block);
}

void my_reset(void) {
    /* Reset entire heap back to a single giant free block */
    my_init();
}

size_t my_total_free(void) {
    if (!heap_initialized) {
        return HEAP_SIZE - BLOCK_HEADER_SIZE;
    }

    size_t total = 0;
    struct Block *curr = heap_head;
    while (curr != NULL) {
        if (curr->free) {
            total += curr->size;
        }
        curr = curr->next;
    }
    return total;
}

size_t my_largest_free(void) {
    if (!heap_initialized) {
        return HEAP_SIZE - BLOCK_HEADER_SIZE;
    }

    size_t largest = 0;
    struct Block *curr = heap_head;
    while (curr != NULL) {
        if (curr->free && curr->size > largest) {
            largest = curr->size;
        }
        curr = curr->next;
    }
    return largest;
}

double my_fragmentation(void) {
    size_t total = my_total_free();
    if (total == 0) {
        return 0.0;
    }
    size_t largest = my_largest_free();
    return 1.0 - ((double)largest / (double)total);
}

void my_dump_heap(void) {
    printf("\n=== HEAP DUMP ===\n");
    printf("Heap Base: %p | Heap End: %p | Capacity: %d bytes (1 MB)\n",
           (void *)heap, (void *)(heap + HEAP_SIZE), HEAP_SIZE);
    printf("Active Strategy: %s\n", current_strategy == FIRST_FIT ? "FIRST_FIT" : "BEST_FIT");
    printf("------------------------------------------------------------------------------------\n");
    printf("%-5s %-16s %-16s %-10s %-8s %-16s %-16s\n",
           "Index", "Block Address", "Payload Addr", "Size (bytes)", "Status", "Prev", "Next");
    printf("------------------------------------------------------------------------------------\n");

    if (!heap_initialized) {
        printf("(Heap not yet initialized - 1 MB unallocated)\n");
        return;
    }

    struct Block *curr = heap_head;
    int idx = 0;
    size_t total_used = 0;
    size_t total_free = 0;

    while (curr != NULL) {
        printf("%-5d %-16p %-16p %-10zu %-8s %-16p %-16p\n",
               idx++,
               (void *)curr,
               block_to_payload(curr),
               curr->size,
               curr->free ? "FREE" : "USED",
               (void *)curr->prev,
               (void *)curr->next);

        if (curr->free) {
            total_free += curr->size;
        } else {
            total_used += curr->size;
        }
        curr = curr->next;
    }

    printf("------------------------------------------------------------------------------------\n");
    printf("Summary: Blocks=%d | Free Memory=%zu B | Used Memory=%zu B | Fragmentation=%.4f\n",
           idx, total_free, total_used, my_fragmentation());
    printf("=================\n\n");
}
