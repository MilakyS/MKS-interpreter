# MKS Execution Plans

This file stores the current/most recent execution plan.

## Current Plan

### Objective
Promote real `bool` into the MKS core, migrate runtime/std/test surface to `true`/`false`, and remove `std.bool` from the canonical std surface.

### Non-goals
- No redesign of numeric truthiness for existing `if/while` compatibility
- No new package/plugin work
- No unrelated language feature work

### Affected files
- .agent/PLANS.md
- .agent/lessons.md
- Lexer/
- Parser/
- Eval/
- Runtime/
- std/
- docs/SEMANTICS.md
- docs/REFERENCE.md
- docs/USER_GUIDE.md
- tests/cases/
- tests/expected/
- tests/unit/test_runtime.c

### Implementation steps
1. Finish core bool plumbing across lexer, parser, AST, eval, runtime values, operators, and output.
2. Update builtins and stdlib so conversions/JSON/file APIs use real bool values.
3. Remove `std.bool` from canonical std imports and module registries.
4. Migrate source-level and unit tests from fake numeric bool output to `true`/`false`.
5. Update docs to state the stable bool contract and JSON impact.
6. Rebuild and run unit/integration tests.
7. Review the diff against `.agent/review.md`.
8. Update `.agent/lessons.md` only if a concrete reusable lesson emerges.

### Risks
- Numeric truthiness is still used widely; replacing it completely would create avoidable breakage.
- `expect(...)`, JSON, and output formatting must agree on bool behavior or the surface will split.
- `std.bool` removal must not leave stale module descriptors or tests behind.

### Tests
- `cmake --build build -j2`
- `./build/mks_unit_tests`
- `ctest --test-dir build --output-on-failure`
- `./tests.sh`
- New targeted tests:
  - `true`/`false` parse as core literals
  - logic/comparison operators print `true`/`false`
  - `Int(true)` / `String(false)` use the new contract
  - JSON bool parse/stringify round-trips as bool

### Review result
Pending.

### Lessons learned
Pending.

## Template

### Objective

### Non-goals

### Affected files

### Implementation steps

### Risks

### Tests

### Review result

### Lessons learned
