# Performance Stats

| Allocator | Iterations | Time (ms) | Ops/sec |
| --- | ---: | ---: | ---: |
| slab+buddy | 100000 | 96.850 | 2065049 |
| libc malloc/free | 100000 | 55.776 | 3585772 |

## sbmalloc counters

- Arena size: 536870912 bytes
- Peak used bytes: 276058688
- Slab allocations: 6253
- Buddy allocations: 93747
- Failed allocations: 0
