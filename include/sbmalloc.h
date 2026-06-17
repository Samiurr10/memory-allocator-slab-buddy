#ifndef SBMALLOC_H
#define SBMALLOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SBMALLOC_DEFAULT_ARENA_SIZE (16u * 1024u * 1024u)

typedef struct sbmalloc_stats {
    size_t arena_size;
    size_t used_bytes;
    size_t peak_used_bytes;
    size_t free_bytes;
    size_t slab_allocations;
    size_t buddy_allocations;
    size_t allocation_count;
    size_t free_count;
    size_t failed_allocations;
} sbmalloc_stats;

int sbmalloc_init(size_t arena_size);
void sbmalloc_destroy(void);

void *sb_malloc(size_t size);
void sb_free(void *ptr);
void *sb_calloc(size_t count, size_t size);
void *sb_realloc(void *ptr, size_t size);

sbmalloc_stats sbmalloc_get_stats(void);
int sbmalloc_check(void);

#ifdef __cplusplus
}
#endif

#endif
