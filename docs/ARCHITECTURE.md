# MKS Architecture Contract

This document defines current ownership boundaries inside the MKS interpreter.

## Layer ownership

- `Lexer/`
  - owns tokenization
  - does not own runtime meaning

- `Parser/`
  - owns syntax recognition
  - owns AST shape
  - does not own value semantics or storage policy

- `Eval/`
  - owns AST evaluation order
  - owns control-flow propagation through runtime values
  - does not own long-term storage layout or GC policy

- `Runtime/`
  - owns runtime value operations
  - owns builtins, module loading entry points, object behavior, conversion behavior, and user-visible runtime errors
  - owns module-cache lifetime and long-lived callback/extension registries

- `env/`
  - owns scoped name storage
  - owns name lookup chains and variable binding containers
  - does not own parsing or user-facing syntax

- `GC/`
  - owns allocation metadata, marking, sweeping, rooting, and object lifetime traversal
  - does not own language syntax or evaluation policy

- `Utils/`
  - owns low-level helpers that do not define language semantics

## Entry-point contract

- `main.c` should stay thin.
- Reusable execution logic belongs in runtime/context/runner-level helpers, not in CLI-only code.
- Features should not depend on ad hoc CLI state when they are part of language behavior.

## Acceptable coupling

- `Parser/` may shape the AST that `Eval/` consumes.
- `Eval/` may call runtime helpers to implement semantics.
- `Runtime/` may use `env/` and `GC/` primitives.

## Unacceptable coupling

- parser nodes carrying hidden runtime-owned state
- GC policy embedded in parser or lexer logic
- module/package resolution smeared across unrelated subsystems without a single runtime owner
- user-visible semantics implemented as CLI-only special cases

## Language direction constraints

MKS is interpreter-first, not interpreter-only.

Architecture should prefer:
- explicit runtime behavior
- value/reference models that can later map to bytecode or native code
- subsystem boundaries that remain viable if a VM or compiler backend is added later

## Design in progress

These areas exist but are not yet frozen as long-term architecture:

- plugin/native module extension surface
- final package-management ownership boundaries
- whether some current runtime helpers become public embedding APIs
