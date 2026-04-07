## 2025-05-15 - Environment Hash Table Indexing Optimization
**Learning:** Replacing the modulo operator (%) with bitwise AND (&) for hash table indexing provides a measurable performance boost (approx. 7% in recursive Fibonacci benchmark) when the bucket count is guaranteed to be a power of two.
**Action:** Always ensure hash table bucket counts are powers of two and use bitwise AND for indexing in performance-critical paths.
