#ifndef MEMORY_H
#define MEMORY_H

/* =============================================================================
 * memory.h  —  Memory Management Module Public Interface
 * ============================================================================= */

void  memory_init   (int total_size);  /* boot: allocate virtual RAM slab      */
void *t_alloc       (int size);        /* allocate `size` bytes from virtual RAM */
void  t_dealloc     (void *ptr);       /* free a previously t_alloc'd pointer   */
void  memory_cleanup(void);            /* shutdown: release the RAM slab        */

#endif /* MEMORY_H */
