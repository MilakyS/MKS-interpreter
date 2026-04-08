## 2025-05-15 - [Environment Hash Table Indexing Optimization]
**Learning:** Replacing the modulo operator (`%`) with bitwise AND (`&`) for hash table indexing provides a significant performance boost (~10% in recursive Fibonacci benchmarks) when bucket counts are guaranteed to be powers of two.
**Action:** Always ensure hash table bucket counts are powers of two and use bitwise AND for indexing in performance-critical paths. Add assertions to enforce the power-of-two invariant.
