#include "sbmalloc.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ALIGNMENT 16u
#define MIN_ORDER 5u
#define PAGE_ORDER 12u
#define PAGE_SIZE (1u << PAGE_ORDER)
#define MAX_ORDERS 64u
#define SLAB_CLASSES 5u
#define BUDDY_FREE_MAGIC 0xBADDCAFEu
#define SLAB_MAGIC 0x51AB51ABu
#define ALLOC_MAGIC 0xA110CA7Eu

typedef struct BuddyBlock {
    uint32_t magic;
    uint32_t order;
    struct BuddyBlock *prev;
    struct BuddyBlock *next;
} BuddyBlock;

typedef struct FreeObject {
    struct FreeObject *next;
} FreeObject;

typedef struct SlabPage {
    uint32_t magic;
    uint16_t class_index;
    uint16_t reserved;
    size_t object_size;
    size_t capacity;
    size_t free_count;
    struct SlabPage *next;
    FreeObject *free_list;
} SlabPage;

typedef struct AllocHeader {
    uint32_t magic;
    uint32_t order;
    size_t requested;
} AllocHeader;

typedef struct Allocator {
    unsigned char *arena;
    size_t arena_size;
    unsigned min_order;
    unsigned max_order;
    BuddyBlock *free_lists[MAX_ORDERS];
    SlabPage *slabs[SLAB_CLASSES];
    sbmalloc_stats stats;
    int initialized;
} Allocator;

static Allocator g_alloc;
static const size_t g_slab_sizes[SLAB_CLASSES] = {16u, 32u, 64u, 128u, 256u};

static size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static int is_power_of_two(size_t value) {
    return value && ((value & (value - 1u)) == 0u);
}

static size_t next_power_of_two(size_t value) {
    size_t power = 1u;
    while (power < value) {
        power <<= 1u;
    }
    return power;
}

static unsigned order_for_size(size_t size) {
    unsigned order = MIN_ORDER;
    size_t block_size = (size_t)1u << order;
    while (block_size < size) {
        block_size <<= 1u;
        order++;
    }
    return order;
}

static void free_list_push(unsigned order, unsigned char *block) {
    BuddyBlock *node = (BuddyBlock *)block;
    node->magic = BUDDY_FREE_MAGIC;
    node->order = order;
    node->prev = NULL;
    node->next = g_alloc.free_lists[order];
    if (node->next) {
        node->next->prev = node;
    }
    g_alloc.free_lists[order] = node;
}

static int free_list_remove(unsigned order, BuddyBlock *target) {
    if (!target || target->magic != BUDDY_FREE_MAGIC || target->order != order) {
        return 0;
    }
    if (target->prev) {
        target->prev->next = target->next;
    } else {
        g_alloc.free_lists[order] = target->next;
    }
    if (target->next) {
        target->next->prev = target->prev;
    }
    target->prev = NULL;
    target->next = NULL;
    return 1;
}

static void *buddy_alloc(size_t size, unsigned *out_order) {
    unsigned order = order_for_size(size);
    unsigned current = order;

    if (!g_alloc.initialized || order > g_alloc.max_order) {
        g_alloc.stats.failed_allocations++;
        return NULL;
    }

    while (current <= g_alloc.max_order && !g_alloc.free_lists[current]) {
        current++;
    }
    if (current > g_alloc.max_order) {
        g_alloc.stats.failed_allocations++;
        return NULL;
    }

    BuddyBlock *block = g_alloc.free_lists[current];
    g_alloc.free_lists[current] = block->next;

    while (current > order) {
        current--;
        unsigned char *base = (unsigned char *)block;
        unsigned char *buddy = base + ((size_t)1u << current);
        free_list_push(current, buddy);
    }

    block->magic = 0u;
    block->order = 0u;
    block->prev = NULL;
    block->next = NULL;
    if (out_order) {
        *out_order = order;
    }
    return block;
}

static void buddy_free(void *ptr, unsigned order) {
    unsigned char *block = (unsigned char *)ptr;

    while (order < g_alloc.max_order) {
        size_t offset = (size_t)(block - g_alloc.arena);
        size_t buddy_offset = offset ^ ((size_t)1u << order);
        BuddyBlock *buddy = (BuddyBlock *)(g_alloc.arena + buddy_offset);

        if (buddy->magic != BUDDY_FREE_MAGIC || buddy->order != order) {
            break;
        }
        if (!free_list_remove(order, buddy)) {
            break;
        }
        if (buddy_offset < offset) {
            block = (unsigned char *)buddy;
        }
        order++;
    }

    free_list_push(order, block);
}

static int slab_class_for_size(size_t size) {
    for (size_t i = 0; i < SLAB_CLASSES; i++) {
        if (size <= g_slab_sizes[i]) {
            return (int)i;
        }
    }
    return -1;
}

static SlabPage *new_slab_page(size_t class_index) {
    unsigned order = 0u;
    SlabPage *page = (SlabPage *)buddy_alloc(PAGE_SIZE, &order);
    if (!page || order != PAGE_ORDER) {
        return NULL;
    }

    size_t object_size = align_up(g_slab_sizes[class_index], ALIGNMENT);
    size_t start = align_up(sizeof(SlabPage), ALIGNMENT);
    size_t capacity = (PAGE_SIZE - start) / object_size;

    page->magic = SLAB_MAGIC;
    page->class_index = (uint16_t)class_index;
    page->reserved = 0u;
    page->object_size = object_size;
    page->capacity = capacity;
    page->free_count = capacity;
    page->next = g_alloc.slabs[class_index];
    page->free_list = NULL;

    unsigned char *cursor = (unsigned char *)page + start;
    for (size_t i = 0; i < capacity; i++) {
        FreeObject *object = (FreeObject *)(cursor + i * object_size);
        object->next = page->free_list;
        page->free_list = object;
    }

    g_alloc.slabs[class_index] = page;
    return page;
}

static void unlink_slab_page(SlabPage *page) {
    SlabPage **cursor = &g_alloc.slabs[page->class_index];
    while (*cursor) {
        if (*cursor == page) {
            *cursor = page->next;
            return;
        }
        cursor = &(*cursor)->next;
    }
}

static void *slab_alloc(size_t size) {
    int class_index = slab_class_for_size(size);
    if (class_index < 0) {
        return NULL;
    }

    SlabPage *page = g_alloc.slabs[class_index];
    while (page && !page->free_list) {
        page = page->next;
    }
    if (!page) {
        page = new_slab_page((size_t)class_index);
    }
    if (!page) {
        return NULL;
    }

    FreeObject *object = page->free_list;
    page->free_list = object->next;
    page->free_count--;
    memset(object, 0xA5, page->object_size);
    g_alloc.stats.slab_allocations++;
    g_alloc.stats.allocation_count++;
    g_alloc.stats.used_bytes += page->object_size;
    if (g_alloc.stats.used_bytes > g_alloc.stats.peak_used_bytes) {
        g_alloc.stats.peak_used_bytes = g_alloc.stats.used_bytes;
    }
    return object;
}

static SlabPage *slab_page_from_ptr(void *ptr) {
    uintptr_t arena = (uintptr_t)g_alloc.arena;
    uintptr_t address = (uintptr_t)ptr;
    if (address < arena || address >= arena + g_alloc.arena_size) {
        return NULL;
    }
    uintptr_t offset = address - arena;
    SlabPage *page = (SlabPage *)(arena + (offset & ~((uintptr_t)PAGE_SIZE - 1u)));
    if (page->magic == SLAB_MAGIC && page->class_index < SLAB_CLASSES) {
        return page;
    }
    return NULL;
}

int sbmalloc_init(size_t arena_size) {
    if (g_alloc.initialized) {
        sbmalloc_destroy();
    }

    if (arena_size == 0u) {
        arena_size = SBMALLOC_DEFAULT_ARENA_SIZE;
    }
    arena_size = next_power_of_two(align_up(arena_size, PAGE_SIZE));
    if (arena_size < PAGE_SIZE || !is_power_of_two(arena_size)) {
        return -1;
    }

    unsigned max_order = order_for_size(arena_size);
    if (max_order >= MAX_ORDERS) {
        return -1;
    }

    void *arena = NULL;
    if (posix_memalign(&arena, PAGE_SIZE, arena_size) != 0) {
        return -1;
    }

    memset(&g_alloc, 0, sizeof(g_alloc));
    g_alloc.arena = (unsigned char *)arena;
    g_alloc.arena_size = arena_size;
    g_alloc.min_order = MIN_ORDER;
    g_alloc.max_order = max_order;
    g_alloc.initialized = 1;
    g_alloc.stats.arena_size = arena_size;
    free_list_push(max_order, g_alloc.arena);
    return 0;
}

void sbmalloc_destroy(void) {
    free(g_alloc.arena);
    memset(&g_alloc, 0, sizeof(g_alloc));
}

void *sb_malloc(size_t size) {
    if (!g_alloc.initialized && sbmalloc_init(0u) != 0) {
        return NULL;
    }
    if (size == 0u) {
        return NULL;
    }

    void *small = slab_alloc(size);
    if (small) {
        return small;
    }

    size_t header_size = align_up(sizeof(AllocHeader), ALIGNMENT);
    unsigned order = 0u;
    AllocHeader *header = (AllocHeader *)buddy_alloc(size + header_size, &order);
    if (!header) {
        return NULL;
    }
    header->magic = ALLOC_MAGIC;
    header->order = order;
    header->requested = size;
    g_alloc.stats.buddy_allocations++;
    g_alloc.stats.allocation_count++;
    g_alloc.stats.used_bytes += ((size_t)1u << order);
    if (g_alloc.stats.used_bytes > g_alloc.stats.peak_used_bytes) {
        g_alloc.stats.peak_used_bytes = g_alloc.stats.used_bytes;
    }
    return (unsigned char *)header + header_size;
}

void sb_free(void *ptr) {
    if (!ptr || !g_alloc.initialized) {
        return;
    }

    SlabPage *page = slab_page_from_ptr(ptr);
    if (page) {
        FreeObject *object = (FreeObject *)ptr;
        object->next = page->free_list;
        page->free_list = object;
        page->free_count++;
        g_alloc.stats.used_bytes -= page->object_size;
        g_alloc.stats.free_count++;

        if (page->free_count == page->capacity) {
            uint16_t class_index = page->class_index;
            unlink_slab_page(page);
            page->magic = 0u;
            buddy_free(page, PAGE_ORDER);
            (void)class_index;
        }
        return;
    }

    size_t header_size = align_up(sizeof(AllocHeader), ALIGNMENT);
    AllocHeader *header = (AllocHeader *)((unsigned char *)ptr - header_size);
    if (header->magic != ALLOC_MAGIC || header->order > g_alloc.max_order) {
        return;
    }

    unsigned order = header->order;
    header->magic = 0u;
    g_alloc.stats.used_bytes -= ((size_t)1u << order);
    g_alloc.stats.free_count++;
    buddy_free(header, order);
}

void *sb_calloc(size_t count, size_t size) {
    if (count != 0u && size > ((size_t)-1) / count) {
        if (g_alloc.initialized) {
            g_alloc.stats.failed_allocations++;
        }
        return NULL;
    }
    size_t total = count * size;
    void *ptr = sb_malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *sb_realloc(void *ptr, size_t size) {
    if (!ptr) {
        return sb_malloc(size);
    }
    if (size == 0u) {
        sb_free(ptr);
        return NULL;
    }

    size_t old_size = 0u;
    SlabPage *page = slab_page_from_ptr(ptr);
    if (page) {
        old_size = page->object_size;
    } else {
        size_t header_size = align_up(sizeof(AllocHeader), ALIGNMENT);
        AllocHeader *header = (AllocHeader *)((unsigned char *)ptr - header_size);
        if (header->magic != ALLOC_MAGIC) {
            return NULL;
        }
        old_size = header->requested;
    }

    if (size <= old_size) {
        return ptr;
    }

    void *new_ptr = sb_malloc(size);
    if (!new_ptr) {
        return NULL;
    }
    memcpy(new_ptr, ptr, old_size);
    sb_free(ptr);
    return new_ptr;
}

sbmalloc_stats sbmalloc_get_stats(void) {
    sbmalloc_stats stats = g_alloc.stats;
    stats.free_bytes = stats.arena_size >= stats.used_bytes
        ? stats.arena_size - stats.used_bytes
        : 0u;
    return stats;
}

int sbmalloc_check(void) {
    if (!g_alloc.initialized || !g_alloc.arena) {
        return -1;
    }
    for (unsigned order = g_alloc.min_order; order <= g_alloc.max_order; order++) {
        for (BuddyBlock *node = g_alloc.free_lists[order]; node; node = node->next) {
            uintptr_t address = (uintptr_t)node;
            uintptr_t arena = (uintptr_t)g_alloc.arena;
            if (address < arena || address >= arena + g_alloc.arena_size) {
                return -1;
            }
            if (((address - arena) & (((uintptr_t)1u << order) - 1u)) != 0u) {
                return -1;
            }
            if (node->magic != BUDDY_FREE_MAGIC || node->order != order) {
                return -1;
            }
        }
    }
    return 0;
}
