# MKS Codex Loop

Every non-trivial task follows this loop.

## 1. PLAN

Start by writing a short execution brief in your own words.

The brief must contain:
- Goal
- Scope
- Assumptions
- Risks
- Plan
- Tests

Write or update `.agent/PLANS.md`.

Include:
- objective
- non-goals
- affected files
- implementation steps
- risks
- tests

Rules:
- do not silently expand the task
- choose the safest narrow interpretation if the request is ambiguous
- if the task touches GC-managed values, explicitly reason about rooting, pinning, and ownership
- if the task touches modules, explicitly reason about cache lifetime and exports
- if the task touches parser/eval, explicitly reason about AST ownership and runtime values

## 2. EXECUTE

Implement only what the plan says.

Rules:
- small diff
- no unrelated refactor
- no hidden behavior changes
- no deleted tests

## 3. REVIEW

Review the change against `.agent/review.md`.

Fix before finishing:
- memory bugs
- parser ambiguity
- GC rooting problems
- missing tests
- architecture violations

## 4. LEARN

Update `.agent/lessons.md` only if a concrete lesson was learned.

A valid lesson must be:
- specific
- actionable
- testable
- based on the current task
