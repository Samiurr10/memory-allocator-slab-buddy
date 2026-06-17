#include "sbmalloc.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static int test_basic_alloc_free(void) {
    ASSERT_TRUE(sbmalloc_init(1u << 20) == 0);
    void *a = sb_malloc(24);
    void *b = sb_malloc(4097);
    ASSERT_TRUE(a != NULL);
    ASSERT_TRUE(b != NULL);
    ASSERT_TRUE(((uintptr_t)a % 16u) == 0u);
    ASSERT_TRUE(((uintptr_t)b % 16u) == 0u);
    memset(a, 0x11, 24);
    memset(b, 0x22, 4097);
    sb_free(a);
    sb_free(b);
    ASSERT_TRUE(sbmalloc_check() == 0);
    sbmalloc_stats stats = sbmalloc_get_stats();
    ASSERT_TRUE(stats.allocation_count == 2u);
    ASSERT_TRUE(stats.free_count == 2u);
    sbmalloc_destroy();
    return 0;
}

static int test_calloc_zeroes(void) {
    ASSERT_TRUE(sbmalloc_init(1u << 20) == 0);
    unsigned char *data = (unsigned char *)sb_calloc(128, sizeof(unsigned char));
    ASSERT_TRUE(data != NULL);
    for (size_t i = 0; i < 128; i++) {
        ASSERT_TRUE(data[i] == 0u);
    }
    sb_free(data);
    sbmalloc_destroy();
    return 0;
}

static int test_realloc_preserves_data(void) {
    ASSERT_TRUE(sbmalloc_init(1u << 20) == 0);
    char *data = (char *)sb_malloc(32);
    ASSERT_TRUE(data != NULL);
    strcpy(data, "slab-to-buddy");
    data = (char *)sb_realloc(data, 1024);
    ASSERT_TRUE(data != NULL);
    ASSERT_TRUE(strcmp(data, "slab-to-buddy") == 0);
    data = (char *)sb_realloc(data, 2048);
    ASSERT_TRUE(data != NULL);
    ASSERT_TRUE(strcmp(data, "slab-to-buddy") == 0);
    sb_free(data);
    sbmalloc_destroy();
    return 0;
}

static int test_slab_reuse(void) {
    ASSERT_TRUE(sbmalloc_init(1u << 20) == 0);
    void *items[300];
    for (size_t i = 0; i < 300; i++) {
        items[i] = sb_malloc(16);
        ASSERT_TRUE(items[i] != NULL);
    }
    for (size_t i = 0; i < 300; i++) {
        sb_free(items[i]);
    }
    ASSERT_TRUE(sbmalloc_check() == 0);
    sbmalloc_stats stats = sbmalloc_get_stats();
    ASSERT_TRUE(stats.slab_allocations == 300u);
    ASSERT_TRUE(stats.free_count == 300u);
    sbmalloc_destroy();
    return 0;
}

static int test_buddy_coalescing(void) {
    ASSERT_TRUE(sbmalloc_init(1u << 20) == 0);
    void *a = sb_malloc(128u * 1024u);
    void *b = sb_malloc(128u * 1024u);
    ASSERT_TRUE(a != NULL);
    ASSERT_TRUE(b != NULL);
    sb_free(a);
    sb_free(b);
    void *large = sb_malloc(512u * 1024u);
    ASSERT_TRUE(large != NULL);
    sb_free(large);
    ASSERT_TRUE(sbmalloc_check() == 0);
    sbmalloc_destroy();
    return 0;
}

static int test_stress_pattern(void) {
    ASSERT_TRUE(sbmalloc_init(4u << 20) == 0);
    void *slots[512] = {0};
    for (size_t round = 0; round < 2000; round++) {
        size_t index = (round * 37u) % 512u;
        if (slots[index]) {
            sb_free(slots[index]);
            slots[index] = NULL;
        } else {
            size_t size = ((round * 131u) % 8192u) + 1u;
            slots[index] = sb_malloc(size);
            ASSERT_TRUE(slots[index] != NULL);
            memset(slots[index], (int)(round & 0xffu), size);
        }
        ASSERT_TRUE(sbmalloc_check() == 0);
    }
    for (size_t i = 0; i < 512; i++) {
        sb_free(slots[i]);
    }
    sbmalloc_destroy();
    return 0;
}

int main(void) {
    struct {
        const char *name;
        int (*fn)(void);
    } tests[] = {
        {"basic_alloc_free", test_basic_alloc_free},
        {"calloc_zeroes", test_calloc_zeroes},
        {"realloc_preserves_data", test_realloc_preserves_data},
        {"slab_reuse", test_slab_reuse},
        {"buddy_coalescing", test_buddy_coalescing},
        {"stress_pattern", test_stress_pattern},
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        if (tests[i].fn() != 0) {
            fprintf(stderr, "not ok - %s\n", tests[i].name);
            return 1;
        }
        printf("ok - %s\n", tests[i].name);
    }
    return 0;
}
