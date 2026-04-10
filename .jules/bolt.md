
## 2025-05-15 - [Pre-computed hashes for parameter passing]
**Learning:** Using pre-computed hashes from AST nodes during function/method calls avoids redundant string hashing at runtime. This is particularly effective for recursive functions where the overhead of `get_hash()` on every call adds up.
**Action:** Always check if AST nodes already store hashes for identifiers before calling `env_set` or `env_get`.
