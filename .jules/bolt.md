## 2025-05-15 - [Environment Indexing and Parameter Binding Optimization]
**Learning:** Replacing the modulo operator with bitwise AND for hash table indexing provides a measurable performance boost (approx. 11% improvement in microbenchmarks) when the bucket count is guaranteed to be a power of two. Additionally, reusing pre-computed hashes from the AST for function parameter binding avoids redundant string hashing on every function call, which is critical for performance in recursive functions.

**Action:** Always ensure that hash table sizes are powers of two and use bitwise AND for indexing. Pre-compute and store hashes in the AST for all frequently looked-up identifiers (like function parameters and method names) to minimize runtime overhead.
