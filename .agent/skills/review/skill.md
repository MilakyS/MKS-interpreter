# name: review_code
# description: Perform a strict review of code changes

## When to use
Use after implementation and validation.

## Steps

1. Review the diff.
2. Analyze the change against `.agent/review.md`.

## Check

### Architecture
- Does it preserve subsystem separation?
- Does it avoid unnecessary complexity?

### Low-level direction
- Is behavior explicit?
- Is it VM/compiler-friendly?

### C safety
- any dangling pointers?
- unchecked allocations?
- ownership ambiguity?

### GC safety
- are new objects marked?
- are temporary values rooted?
- are environments reachable?

### Parser
- syntax clear?
- AST consistent?

### Tests
- new tests exist?
- edge cases covered?

## Output

- critical issues
- risks
- suggestions

## Rules

- Assume the code is wrong until proven correct.
- Prefer rejecting weak logic over accepting it by default.
