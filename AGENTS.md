# MKS Codex Entry

## Context budget policy

- Do not scan the whole repository.
- Do not read the whole `.agent/` tree.
- Read only the smallest useful set of files for the task.
- Load at most one `.agent/skills/*.md` file unless the task clearly spans multiple subsystems.
- Prefer source files over docs when they disagree.

## Routing

Use these files only when relevant:

- GC / lifetime / roots / pinning:
  - `.agent/skills/gc.md`
- Parser / AST / syntax:
  - `.agent/skills/parser.md`
- Eval / runtime values / control flow:
  - `.agent/skills/eval.md`
- Modules / using / exports / cache:
  - `.agent/skills/modules.md`
- Releases / packaging / roadmap:
  - `.agent/skills/release.md`
- Review-only tasks:
  - `.agent/review.md`

## Required self-brief

Before implementation, write a short execution brief:

- Goal
- Scope
- Assumptions
- Risks
- Plan
- Tests

Keep it under 12 lines.

## Hard rules

- Prefer small local patches.
- Do not redesign unless explicitly requested.
- Do not touch unrelated files.
- Preserve existing public language syntax unless the task asks to change it.
- After C changes, run the smallest relevant test first, then the full test suite if reasonable.
- If build/test commands are unknown, inspect project files instead of guessing.
