# MKS Decisions

This document records active design decisions and unresolved decision points.

## Active decisions

### Interpreter-first, low-level-oriented language

Current decision:
- MKS should behave like a systems-oriented language implemented through an interpreter first.

Implication:
- convenience features should not erase visible runtime behavior

### Root `.agent/` is canonical

Current decision:
- root `.agent/` is the canonical agent operating context

Implication:
- legacy `agents/` content should be migrated or treated as non-canonical

### Namespace-based imports

Current decision:
- `using` binds module namespaces instead of copying exports directly into scope

Implication:
- public module APIs should be read through explicit namespace objects

### Imported modules skip top-level executable statements

Current decision:
- imports load declarations once and skip top-level executable statements

Implication:
- reusable module behavior belongs in exported declarations, not in import-time script execution

### Module cache lifetime

Current decision:
- loaded modules are cached per interpreter context and keep their module environment alive for the whole context lifetime

Implication:
- repeated imports reuse the same exports namespace object
- exported closures may safely capture module-private state without requiring the importer to keep an extra alias alive
- the current implementation favors deterministic lifetime over unload/reload flexibility

### Input API split

Current decision:
- line input and word input are separate builtins

Implication:
- overloaded call shapes like the old numeric mode should not be treated as the canonical direction

### RuntimeValue assignment model

Current decision:
- MKS copies `RuntimeValue` containers at binding/call boundaries instead of deep-copying runtime objects by default

Implication:
- numbers, booleans, and `null` behave as copied scalar values
- arrays, objects, modules, functions, blueprints, strings, and pointers preserve aliasing to shared underlying runtime state unless user code explicitly copies them

### Core booleans

Current decision:
- booleans are core language/runtime values instead of a stdlib shim

Implication:
- `true` and `false` are surface literals
- comparison and logical operators return booleans
- `std.bool` is not part of the canonical std surface
- JSON booleans map directly to core booleans

### Entity baseline

Current decision:
- `entity` is currently the primary high-level object-constructor surface in MKS

Implication:
- small app/domain objects should prefer `entity` over ad hoc `Object()` bags when constructor/method shape matters
- the current object model remains dynamic and field-by-assignment based until a stricter schema model is intentionally introduced

### Null baseline

Current decision:
- `null` is a real surface/runtime value with a deliberately narrow baseline contract

Implication:
- `null` may be used in variables, entity fields, returns, and control flow
- arithmetic and broad comparison semantics for `null` should not be assumed unless they are explicitly documented

### Release posture

Current decision:
- MKS is currently on an experimental language / unstable runtime release line

Implication:
- public wording should use `experimental`, `snapshot`, or `preview` carefully
- the repository must not claim stable language/platform status before the release gates in `docs/RELEASES.md` are met

## Design in progress

### Copy vs reference model

Unknowns:
- whether future language surface will add an explicit borrow/reference passing form distinct from ordinary handle copying
- whether strings should remain part of the shared-handle model if mutable string operations are later added

### Extend baseline

Current decision:
- `extend` currently registers context-global methods for built-in target families (`array`, `string`, `number`)

Implication:
- extension methods are not namespace-scoped after registration
- closure capture for extension methods is part of the current runtime contract
- duplicate method names for the same target family fail at registration instead of overriding
- repeated import of the same module does not re-apply its `extend` declarations because module loading is cached

### Callback/error boundary baseline

Current decision:
- long-lived runtime-managed callbacks in current MKS (`watch` handlers, extension closures, exported module closures) stay alive for the interpreter context lifetime or until their owning registry is cleared

Implication:
- callback lifetime is explicit and conservative
- runtime errors raised inside those paths are not isolated; they abort the current execution run through the normal runtime error boundary

### `extend` inside modules

Unknowns:
- whether `extend` should remain an import-time declarative side effect
- how far `extend` should participate in future package/plugin visibility rules

### `watch`

Unknowns:
- whether `watch` stays a core language feature
- whether its trigger surface should expand beyond direct binding updates

### Package system

Unknowns:
- dependency/version model
- publish/install workflow
- native-module packaging model

### Native extension surface

Unknowns:
- whether the next step is plugin ABI, FFI, or both
- what runtime API can be made stable without exposing internal GC/layout assumptions
