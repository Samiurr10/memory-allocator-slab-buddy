#include "sbmalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef void *(*alloc_fn)(size_t);
typedef void (*free_fn)(void *);

static double elapsed_ms(struct timespec start, struct timespec end) {
    return (double)(end.tv_sec - start.tv_sec) * 1000.0
        + (double)(end.tv_nsec - start.tv_nsec) / 1000000.0;
}

static double run_workload(const char *name, alloc_fn alloc_cb, free_fn free_cb, size_t iterations) {
    void **ptrs = (void **)calloc(iterations, sizeof(void *));
    if (!ptrs) {
        return -1.0;
    }

    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t i = 0; i < iterations; i++) {
        size_t size = ((i * 1103515245u + 12345u) % 4096u) + 1u;
        ptrs[i] = alloc_cb(size);
        if (!ptrs[i]) {
            fprintf(stderr, "%s allocation failed at %zu\n", name, i);
            free(ptrs);
            return -1.0;
        }
        memset(ptrs[i], (int)(i & 0xffu), size);
    }
    for (size_t i = 0; i < iterations; i += 2u) {
        free_cb(ptrs[i]);
        ptrs[i] = NULL;
    }
    for (size_t i = 1; i < iterations; i += 2u) {
        free_cb(ptrs[i]);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    free(ptrs);
    return elapsed_ms(start, end);
}

int main(int argc, char **argv) {
    size_t iterations = 100000u;
    if (argc > 1) {
        iterations = (size_t)strtoull(argv[1], NULL, 10);
    }

    if (sbmalloc_init(512u * 1024u * 1024u) != 0) {
        fprintf(stderr, "failed to initialize sbmalloc\n");
        return 1;
    }
    double sb_ms = run_workload("sbmalloc", sb_malloc, sb_free, iterations);
    sbmalloc_stats stats = sbmalloc_get_stats();
    sbmalloc_destroy();

    double libc_ms = run_workload("libc", malloc, free, iterations);
    if (sb_ms < 0.0 || libc_ms < 0.0) {
        return 1;
    }

    printf("# Performance Stats\n\n");
    printf("| Allocator | Iterations | Time (ms) | Ops/sec |\n");
    printf("| --- | ---: | ---: | ---: |\n");
    printf("| slab+buddy | %zu | %.3f | %.0f |\n", iterations, sb_ms, (iterations * 2.0) / (sb_ms / 1000.0));
    printf("| libc malloc/free | %zu | %.3f | %.0f |\n\n", iterations, libc_ms, (iterations * 2.0) / (libc_ms / 1000.0));
    printf("## sbmalloc counters\n\n");
    printf("- Arena size: %zu bytes\n", stats.arena_size);
    printf("- Peak used bytes: %zu\n", stats.peak_used_bytes);
    printf("- Slab allocations: %zu\n", stats.slab_allocations);
    printf("- Buddy allocations: %zu\n", stats.buddy_allocations);
    printf("- Failed allocations: %zu\n", stats.failed_allocations);
    return 0;
}
