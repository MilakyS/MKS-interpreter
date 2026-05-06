# MKS 0.3 Alpha

MKS 0.3 Alpha is an experimental runtime/performance release.

## Highlights

- Faster VM globals with cached env entries.
- Faster simple counted loops and bulk local/global additions.
- Safe counted-loop guards for `watch` and `defer`.
- Faster function calls through compiled function caching.
- Direct self-call opcode for recursive functions.
- Dynamic VM call frames, value stack, and GC root spans for deep recursion.
- `array.push()` native alias for `array.inject()`.

## Validation

- `cmake --build build`
- `./build/mks_unit_tests`
- `./tests.sh`

## Stability

This is an Alpha snapshot on the experimental `0.x` line. Language and runtime
contracts are still allowed to change before a stable release.
