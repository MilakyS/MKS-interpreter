# Bolt's Journal - MKS Interpreter

## 2025-05-15 - Initial exploration
**Learning:** Initializing the journal for the MKS interpreter.
**Action:** Starting the hunt for performance optimizations.

## 2025-05-15 - O(1) String Length Caching
**Learning:** Caching string length in `ManagedString` significantly improves performance for repetitive string operations like concatenation and `.len()` lookups. In MKS, adding a `size_t len` field reduced a simple string-building benchmark's execution time from ~0.5s to ~0.1s (Release build).
**Action:** Always prefer caching length for immutable or frequently-read heap-allocated data structures (strings, arrays, buffers) in performance-sensitive C runtimes.
