## 2024-11-25 - [Hash optimization for parameters and 'self']
**Learning:** Redundant string hashing during function and method dispatch (for parameters and "self") is a significant overhead in a tree-walking interpreter. Using pre-computed hashes from the AST and caching constant hashes like "self" improves performance.
**Action:** Always check for repeated hashing of constant strings or AST-derived identifiers in hot paths like function/method calls.
