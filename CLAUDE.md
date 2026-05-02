# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

To enable AddressSanitizer + UBSan:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build
```

Produces three targets: `build/mks` (CLI), `build/mks_unit_tests` (C unit tests), `libmks_runtime.a` (static lib).

## Tests

```bash
# Golden/black-box test suite (runs all tests/cases/*.mks and tests/modules/*.mks)
./tests.sh

# C unit tests only
./build/mks_unit_tests

# Via CTest
ctest --test-dir build
```

Add a new golden test by placing a `.mks` file in `tests/cases/` and its expected stdout in `tests/expected/<name>.txt`. Run the smallest relevant test first, then the full suite.

## CLI

```bash
mks <file.mks>                  # Execute a file
mks repl                        # Interactive REPL
mks check <file.mks>            # Parse/check only
mks --vm <file.mks>             # Force VM execution
mks --dump-bytecode <file.mks>  # Print bytecode
mks --profile <file.mks>        # Execute with profiler
```

## Architecture

MKS is a scripting language interpreter written in C23. Execution path: source text → Lexer → Parser (AST) → Eval (runtime) or VM (bytecode). A mark/sweep GC manages all heap objects.

### Layer ownership

| Layer | Owns | Must not touch |
|-------|------|----------------|
| `Lexer/` | tokenization | runtime meaning |
| `Parser/` | syntax recognition, AST shape | value semantics, storage policy |
| `Eval/` | evaluation order, control-flow propagation | long-term storage, GC policy |
| `Runtime/` | value ops, builtins, module loading, user-visible errors, module-cache lifetime | language syntax |
| `env/` | scoped name storage, lookup chains | parsing, user-facing syntax |
| `GC/` | allocation metadata, mark/sweep, rooting, object lifetime traversal | language syntax, eval policy |
| `std/` | native standard library modules (math, fs, io, json, path, process, tty, watch) | — |
| `Utils/` | low-level helpers with no language semantics | — |

`main.c` must stay thin — reusable execution logic belongs in `Runtime/runner.c` or `Runtime/context.c`.

### Key files

- `Runtime/value.c/h` — all runtime value types (int, float, bool, string, array, object, function, pointer)
- `Runtime/runner.c/h` — high-level execution entry points
- `Runtime/vm.c/h` — bytecode VM execution and public VM API
- `Runtime/vm_compile.c` — AST → bytecode compiler entry and emit logic
- `Runtime/vm_chunk.c/h` — bytecode chunk storage/helpers
- `Runtime/vm_compile_can.c/h` — VM capability checking
- `GC/gc.c/h` — allocator, mark/sweep, root registration
- `Parser/parser_core.c`, `parser_expr.c`, `parser_stmt.c` — parser split by concern
- `Parser/AST.c/h` — AST node definitions

### Coupling rules

- Parser may shape the AST that Eval consumes.
- Eval may call Runtime helpers.
- Runtime may use `env/` and `GC/` primitives.
- Parser/Lexer must never carry runtime-owned state.
- User-visible semantics must not be implemented as CLI-only special cases.

## Agent skill routing

Load at most one skill file unless the task spans multiple subsystems:

- GC / lifetime / roots / pinning → `.agent/skills/gc.md`
- Parser / AST / syntax → `.agent/skills/parser.md`
- Eval / runtime values / control flow → `.agent/skills/eval.md`
- Modules / using / exports / cache → `.agent/skills/modules.md`
- Releases / packaging / roadmap → `.agent/skills/release.md`
- Review-only tasks → `.agent/review.md`

## Change discipline

Before implementation write a brief (≤12 lines): Goal, Scope, Assumptions, Risks, Plan, Tests.

- Prefer small local patches; do not redesign unless explicitly asked.
- Do not touch unrelated files.
- Preserve existing public MKS language syntax unless the task explicitly changes it.
- Source files are authoritative over docs when they disagree.

## Test expectations by change type

- **Parser/syntax**: positive case + invalid syntax case + AST consistency check.
- **Runtime semantic**: positive case + error-path case + interaction case (if touching modules, objects, pointers, arrays, or control flow).
- **GC/lifetime**: unit coverage + review against `docs/GC_INVARIANTS.md` + existing suites passing.
- **Module/import**: namespace/export case + failure case + repeat-load thought check.
- **Builtin API change**: compatibility note if call shape changes + tests for new and (intentionally broken) old form.
