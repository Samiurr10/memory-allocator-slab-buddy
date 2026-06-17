# sbmalloc

A compact C memory allocator that combines slab allocation for small objects with a coalescing buddy allocator for larger blocks. It is built as a learning-friendly systems project: the code is small enough to inspect, but complete enough to compile, test, benchmark, and reuse as a fixed-arena allocator.

## Features

- `sb_malloc`, `sb_free`, `sb_calloc`, and `sb_realloc` APIs.
- Fixed-size arena initialized by `sbmalloc_init`.
- Slab allocator for 16, 32, 64, 128, and 256 byte size classes.
- Buddy allocator for page and large allocations with block splitting and coalescing.
- Runtime stats for arena usage, peak usage, slab/buddy allocation counts, and failures.
- Unit tests, deterministic stress test, AddressSanitizer target, and a benchmark harness.

## Build

```sh
make
```

## Run Tests

```sh
make test
make asan
```

Current verification:

```text
ok - basic_alloc_free
ok - calloc_zeroes
ok - realloc_preserves_data
ok - slab_reuse
ok - buddy_coalescing
ok - stress_pattern
```

## Benchmark

```sh
make stats
cat docs/performance.md
```

The benchmark uses a deterministic mixed-size allocation/free workload and compares this allocator with the platform `malloc`/`free`. Results vary by CPU, compiler, OS, and thermal state, so regenerate them on your machine before treating the numbers as meaningful.

Current stats from this repo:

| Allocator | Iterations | Time (ms) | Ops/sec |
| --- | ---: | ---: | ---: |
| slab+buddy | 100000 | 96.850 | 2065049 |
| libc malloc/free | 100000 | 55.776 | 3585772 |

More details are in [docs/performance.md](docs/performance.md).

## API Example

```c
#include "sbmalloc.h"

int main(void) {
    if (sbmalloc_init(16 * 1024 * 1024) != 0) {
        return 1;
    }

    char *message = sb_malloc(64);
    if (message) {
        message[0] = 'O';
        message[1] = 'K';
        message[2] = '\0';
    }

    sb_free(message);
    sbmalloc_destroy();
    return 0;
}
```

## Design Notes

Small allocations are served from slab pages. Each page comes from the buddy allocator and is divided into same-size objects, which keeps small allocations fast and reduces per-object metadata.

Large allocations go directly through the buddy allocator. Free blocks live in order-indexed free lists. On free, the allocator checks the matching buddy block and coalesces recursively when both buddies are free.

## Project Layout

```text
include/sbmalloc.h      Public API
src/sbmalloc.c          Slab + buddy allocator implementation
tests/test_allocator.c  Unit and stress tests
bench/bench_allocator.c Benchmark harness
Makefile                Build, test, sanitizer, and stats targets
```

## References

This project follows common allocator structure used in buddy and slab examples: a large arena is acquired once, then allocator-specific metadata manages sub-allocations. Buddy systems provide predictable split/coalesce behavior for power-of-two blocks, while slab pages optimize repeated small object allocations.

- [Buddy memory allocation](https://en.wikipedia.org/wiki/Buddy_memory_allocation)
- [Memory Allocation Strategies, Part 6: Buddy Allocators](https://www.gingerbill.org/article/2021/12/02/memory-allocation-strategies-006/)
- [The Slab Allocator: An Object-Caching Kernel Memory Allocator](https://people.eecs.berkeley.edu/~kubitron/courses/cs194-24-S14/hand-outs/bonwick_slab.pdf)
- [Linux Kernel Archives: Slab Allocator](https://www.kernel.org/doc/gorman/html/understand/understand011.html)
