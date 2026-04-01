## 2026-04-01 - Reduced Environment Hash Table Size
**Learning:** The interpreter was allocating 2KB+ for every new environment because of a large fixed-size hash table (TABLE_SIZE=256). In recursive functions like Fibonacci, this led to massive memory overhead and frequent GC cycles.
**Action:** Reduced TABLE_SIZE to 16. This decreased environment allocation size from 2080 bytes to 160 bytes, resulting in a ~60% speedup in Fibonacci benchmarks (from 3.8s to 1.45s) and significantly reduced GC pressure (collections dropped from 5578 to 413).
