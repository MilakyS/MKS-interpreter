# MKS Test Policy

This document defines what kind of validation is expected for each class of change.

## Test layers

- unit/runtime tests
  - C-level behavior checks
  - status/error boundary checks
  - GC/runtime helper checks

- black-box language cases
  - source-level behavior checks through `tests.sh`
  - visible language semantics

- documentation/meta checks
  - file existence
  - path consistency
  - diff-scope review

## Change-to-test mapping

### Parser or syntax change

Expect:
- at least one positive syntax case
- at least one invalid syntax case
- review of AST consistency impact

### Runtime semantic change

Expect:
- at least one positive observable behavior case
- at least one invalid/error-path case
- one interaction case if the feature touches modules, objects, pointers, arrays, or control flow

### GC or lifetime-sensitive change

Expect:
- focused unit/runtime coverage
- explicit review against GC invariants
- existing test suites still passing
- if the change affects long-lived registries or cached modules, validate both retention and cleanup expectations

### Module/package/import change

Expect:
- namespace/export case
- failure case
- repeat-load or cycle-sensitive thought check if applicable
- if module-private state is exported through closures, add a GC/lifetime-sensitive check when the implementation touches module retention

### Builtin API change

Expect:
- compatibility note if an existing call shape changes
- tests for the new API surface
- tests for the rejected legacy form when behavior intentionally breaks

### Docs-only or agent-only change

Expect:
- no runtime validation by default unless the docs describe commands or behavior that changed
- file/path consistency review
- diff-scope review

## Minimum acceptance rule

A change is not validated just because the happy path works.

For semantic work, validation should cover:
- the intended behavior
- a failure boundary
- the most likely interaction that could invalidate the feature

## Design in progress

The repository does not yet define a complete stress-test policy for:
- long-running GC pressure
- fuzzing of parser/runtime boundaries
- formal performance regression gates
