# Bolt's Journal - Critical Learnings

## 2025-01-24 - [Env Hash Indexing Optimization]
**Learning:** Replaced modulo (%) with bitwise AND (&) for environment hash table indexing.
**Action:** Always ensure the bucket count is a power of two when using bitwise AND for indexing. Added assertions to `env/env.c` to safeguard this requirement. Measured a ~11% speedup in a tight loop benchmark.
