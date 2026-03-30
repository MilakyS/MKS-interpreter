## 2025-05-15 - [ManagedString Length Optimization]
**Learning:** ManagedString initially lacked a length field, causing $O(n)$ performance for `.len()` and repeated $O(n)$ scans during concatenation and other string operations. `make_string` also unnecessarily processed escape sequences even for internal string operations (like results of concatenation).
**Action:** Add a `len` field to `ManagedString` and provide a fast path (`make_string_len`) for string creation that skips escape processing and uses known lengths.
