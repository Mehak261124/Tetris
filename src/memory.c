/* =============================================================================
 * memory.c  —  Memory Management Module
 * =============================================================================
 * OS MODULE: Memory Management
 *   Simulates the memory-management unit (MMU) of a real OS.
 *   Owns a single large "virtual RAM" block acquired once from the host OS
 *   via malloc(), then sub-divides it internally using a free-list allocator.
 *   Nothing else in the project may call malloc() or free() directly.
 *
 * ALLOCATOR DESIGN: First-Fit Free-List with Block Splitting & Coalescing
 *   ┌──────────────┬─────────────────────────────────┐
 *   │ BlockHeader  │         payload (user data)      │
 *   └──────────────┴─────────────────────────────────┘
 *   Each allocation is preceded by a BlockHeader that stores its size,
 *   whether it is free, and a pointer to the next block header.
 *   t_alloc  — first-fit search; splits large blocks to reduce waste.
 *   t_dealloc — marks block free; coalesces adjacent free blocks.
 *
 * RULES COMPLIANCE:
 *   - <stdlib.h> : used ONLY for the single initial malloc() / free() call.
 *   - No other standard allocation functions used anywhere in the project.
 * ============================================================================= */

#include "../include/memory.h"
#include <stdlib.h>   /* malloc / free — one-time virtual RAM block only */

/* ---------------------------------------------------------------------------
 * BlockHeader — metadata prepended to every allocation inside virtual RAM.
 *
 *   size    : usable bytes in the payload (excludes sizeof(BlockHeader)).
 *   is_free : 1 = available for allocation, 0 = in use.
 *   next    : intrusive linked-list pointer to the next block header.
 * --------------------------------------------------------------------------- */
typedef struct BlockHeader {
    int                  size;
    int                  is_free;
    struct BlockHeader  *next;
} BlockHeader;

/* Module-level state — kept static so nothing outside this file can touch it */
static void        *virtual_ram = NULL;  /* base pointer of the RAM slab      */
static int          total_ram   = 0;     /* total bytes in the slab            */
static BlockHeader *head        = NULL;  /* first block in the free list       */

/* Minimum alignment for all allocations (matches 64-bit pointer size).
   Prevents misaligned access faults on structs that contain pointers. */
#define ALIGN_BYTES 8

/* ---------------------------------------------------------------------------
 * memory_init(total_size)
 *   Boot the memory subsystem.  Acquires one large slab from the host OS,
 *   then initialises the free list with a single block covering the whole slab.
 *
 *   Parameters:
 *     total_size — bytes to request from the host OS (e.g. 1 MB = 1048576).
 *
 *   Idempotent: a second call is silently ignored (already initialised guard).
 *
 *   Error handling: if malloc fails, virtual_ram stays NULL and all subsequent
 *   t_alloc() calls will safely return NULL (callers must check).
 * --------------------------------------------------------------------------- */
void memory_init(int total_size) {
    if (virtual_ram != NULL) return;      /* already initialised — do nothing */

    virtual_ram = malloc((size_t)total_size);
    if (!virtual_ram) return;             /* host OS out of memory — degrade gracefully */

    total_ram = total_size;

    /* Treat the very start of the slab as the first (and only) BlockHeader.
       The whole slab minus the header itself is one big free block. */
    head           = (BlockHeader *)virtual_ram;
    head->size     = total_size - (int)sizeof(BlockHeader);
    head->is_free  = 1;
    head->next     = NULL;
}

/* ---------------------------------------------------------------------------
 * t_alloc(size)
 *   Allocate `size` bytes from virtual RAM (first-fit strategy).
 *
 *   Steps:
 *     1. Round `size` up to the next ALIGN_BYTES multiple.
 *     2. Walk the free list looking for the first block large enough.
 *     3. If the block has room to spare, split it into two blocks so the
 *        leftover stays available for future allocations.
 *     4. Mark the chosen block as used and return a pointer to its payload.
 *
 *   Returns: pointer to usable memory, or NULL if:
 *     - memory_init() was never called / failed.
 *     - size ≤ 0.
 *     - no contiguous free region is large enough (out of virtual memory).
 * --------------------------------------------------------------------------- */
void *t_alloc(int size) {
    if (!head || size <= 0) return NULL;   /* guard: bad state or bad argument */

    /* ---- 1. Align requested size ---------------------------------------- */
    int rem = size % ALIGN_BYTES;
    if (rem != 0) size += (ALIGN_BYTES - rem);   /* round up to ALIGN_BYTES   */

    /* ---- 2. First-fit search -------------------------------------------- */
    BlockHeader *current = head;
    while (current) {
        if (current->is_free && current->size >= size) {

            /* ---- 3. Split if enough room remains ------------------------- */
            /* Only split when the leftover would fit at least one more header
               plus the minimum alignment unit — avoids creating unusable splinters. */
            if (current->size >= size + (int)sizeof(BlockHeader) + ALIGN_BYTES) {

                /* New header lives immediately after this block's payload */
                BlockHeader *split = (BlockHeader *)((char *)current
                                                     + sizeof(BlockHeader) + size);
                split->size    = current->size - size - (int)sizeof(BlockHeader);
                split->is_free = 1;
                split->next    = current->next;

                current->size  = size;
                current->next  = split;
            }

            /* ---- 4. Mark as used and return payload pointer -------------- */
            current->is_free = 0;
            return (void *)((char *)current + sizeof(BlockHeader));
        }
        current = current->next;
    }

    return NULL;   /* no fitting block found — out of virtual memory */
}

/* ---------------------------------------------------------------------------
 * t_dealloc(ptr)
 *   Free a previously allocated block and merge adjacent free blocks
 *   (coalescing) to combat fragmentation.
 *
 *   Parameters:
 *     ptr — pointer previously returned by t_alloc().  Passing NULL or an
 *           invalid pointer is silently ignored (matches free() behaviour).
 *
 *   Coalescing: after marking the block free, we do a single pass over the
 *   list merging consecutive free pairs.  This keeps the allocator from
 *   slowly fragmenting into tiny unusable slivers.
 * --------------------------------------------------------------------------- */
void t_dealloc(void *ptr) {
    if (!ptr || !head) return;    /* guard: nothing to free */

    /* Recover the BlockHeader that sits just before the payload */
    BlockHeader *block = (BlockHeader *)((char *)ptr - sizeof(BlockHeader));
    block->is_free = 1;           /* mark the block as available again */

    /* ---- Coalescing pass: merge adjacent free blocks -------------------- */
    BlockHeader *current = head;
    while (current && current->next) {
        if (current->is_free && current->next->is_free) {
            /* Absorb next block: add its header size + its payload size */
            current->size += (int)sizeof(BlockHeader) + current->next->size;
            current->next  = current->next->next;
            /* Do NOT advance current — we may need to merge again with new next */
        } else {
            current = current->next;
        }
    }
}

/* ---------------------------------------------------------------------------
 * memory_cleanup()
 *   Shut down the memory subsystem.  Releases the whole slab back to the
 *   host OS.  Must be called once at program exit (after all t_dealloc calls)
 *   to prevent a leak visible in Valgrind / ASAN reports.
 * --------------------------------------------------------------------------- */
void memory_cleanup(void) {
    if (virtual_ram) {
        free(virtual_ram);    /* the ONE permitted free() call in the project */
        virtual_ram = NULL;
        head        = NULL;
        total_ram   = 0;
    }
}
