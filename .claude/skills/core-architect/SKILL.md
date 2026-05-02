---
name: mks-core-architect
description: Guide large MKS architecture changes across VM, GC, runtime, modules, stdlib, compiler, packages, and future systems features without turning them into unsafe rewrites.
---

# MKS Core Architect

## When to Use

Use this skill when designing or changing major MKS systems:

- VM architecture
- Orbit GC
- runtime values
- module/package system
- stdlib architecture
- native modules
- compiler / bytecode / future JIT
- entity/object model
- pointer model
- capsules / sandboxing
- embedding API
- OS / hardware-facing features
- large cross-directory refactors

## Goal

Turn big ideas into small, safe, testable implementation stages.

The architect’s job is not to invent the coolest design.

The job is to make the coolest design actually land in the repo.

## Core Rule

Never rewrite the core in one huge patch.

Every architecture change must have:

- clear invariant
- minimal first stage
- compatibility plan
- rollback path
- tests
- measurable success condition

---

# Architecture Principles

## 1. Preserve Current MKS Semantics

Do not break existing behavior unless the change is explicitly a breaking language change.

Protect:

- `=:`
- `->` / `<-`
- `?=` / `!?`
- arrays and objects as handle-like values
- numbers/bools/null as value-like values
- pointer read/write behavior
- modules as namespaces
- `entity`, `self`, `init`
- `watch` / `defer`
- tree-walk and VM observable equivalence

## 2. Prefer Layered Evolution

Good architecture path:

```text
telemetry -> compatibility layer -> new internal path -> tests -> default enable

Bad architecture path:

delete old system -> rewrite everything -> hope tests pass
3. Keep Full Fallbacks

For risky systems, keep fallback modes:

full mark-sweep fallback for Orbit GC
tree-walk fallback for VM/compiler changes
old resolver fallback during module resolver migration
non-optimized bytecode path for optimizer changes
4. Design Around Invariants

Every system needs a small set of invariants.

Example GC invariants:

Every live GC object is reachable from stack, env, root, pinned root, or marked object.
Sweep never frees pinned/runtime/module roots.
No young-only collection before write barriers exist.

Example VM invariants:

Every opcode has known length.
Every opcode has known stack effect.
VM and tree-walk produce same observable result.

Example module invariants:

Same resolved module path maps to same exports object.
stdlib resolution cannot be shadowed accidentally.
module env stays alive while exports are reachable.
Required Planning Format

Before implementing a core change, produce:

[MKS CORE ARCHITECTURE PLAN]

Goal:
Current behavior:
Proposed behavior:
Non-goals:

Core invariant:
Compatibility risk:
Rollback strategy:

Stage 1:
Stage 2:
Stage 3:

Files likely touched:
Tests required:
Docs required:
Success metric:
Required Checks
1. Scope Control

Check:

Can this be done in smaller stages?
Can telemetry land before behavior changes?
Can compatibility wrappers avoid touching every subsystem?
Can old and new paths coexist temporarily?
Is this patch mixing architecture, cleanup, and formatting?

Reject patches that do too much at once.

2. Runtime Integration

For any core runtime change, verify:

RuntimeValue representation remains valid
GC can mark every new structure
native methods still obey error boundaries
module exports remain reachable
object/array/string aliasing still works
pointers still observe mutations correctly
3. VM / Compiler Integration

For VM-related architecture:

define bytecode format changes
define opcode stack effects
keep disassembler synced
keep dump/debug output useful
ensure tree-walk equivalence or document VM-only behavior
keep non-optimized path for comparison
4. GC Integration

For GC architecture:

full mark-sweep remains correct
new metadata does not affect collection before tested
pinned roots/envs are respected
module cache and closures are marked
write barriers exist before generational collection
debug/telemetry can prove behavior
5. Module / Package Integration

For resolver/package changes:

stdlib path precedence is explicit
package namespace is explicit
resolved paths are canonicalized
cache keys are stable
error messages show searched locations
imports do not silently load wrong module
6. Stdlib / Native Integration

For stdlib changes:

native modules use runtime errors, not exit
APIs are documented
names match docs/tests
allocated return values are rooted safely
host resources have clear lifetime
platform-specific code has fallback/error message
7. Tooling / Release Impact

For large changes, check:

CMake updated
tests updated
release packaging still includes required files
docs/examples still run
CI catches failure
website does not document unsupported behavior
Design Patterns
Pattern: Telemetry First

Use when implementing risky runtime systems.

Add metadata and debug output first.
Do not change behavior yet.
Verify with tests and examples.
Then use metadata for behavior.

Good for:

Orbit GC
profiler
optimizer decisions
module cache analysis
Pattern: Compatibility Wrapper

Use when replacing internal APIs.

Old API -> wrapper -> new implementation

Allows incremental migration.

Good for:

module resolver
stdlib registration
VM compile pipeline
GC allocation API
Pattern: Dual Path

Use when correctness is risky.

old path and new path both exist
compare outputs
switch default later

Good for:

VM vs tree-walk
optimized vs unoptimized bytecode
old resolver vs new resolver
full GC vs young GC
Pattern: Narrow MVP

Use for big ideas.

Bad MVP:

Implement Orbit GC fully.

Good MVP:

Add object age/survival telemetry and print orbit distribution after full GC.

Bad MVP:

Add package manager.

Good MVP:

Resolve `using pkg.X as x` from `mks_modules/X/mod.mks` with clear errors.
Red Flags

Stop and redesign if you see:

replacing multiple subsystems at once
removing fallback before new path is proven
architecture based on imagined future syntax
new runtime structure not marked by GC
module cache storing raw env without pin/mark
VM optimization changing language behavior
docs updated before implementation works
“temporary” global state without lifetime plan
path resolution with unsafe fallback
native code that can kill host process
no rollback plan
no tests for old behavior
Output Format for Reviews

When reviewing a core architecture change, respond with:

[MKS CORE ARCHITECT REVIEW]

Verdict:
- accept
- accept with changes
- reject / split patch

What this changes:
Core invariant affected:
Main risk:

Required split:
1.
2.
3.

Missing tests:
Docs impact:
Rollback path:

Concrete next patch:
Final Rule

MKS core architecture should evolve like a runtime, not explode like a rewrite.

Small stages. Strong invariants. Clear fallback. Tests before confidence.
