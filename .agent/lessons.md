# MKS Codex Lessons

Reusable lessons learned from real tasks.

Rules:
- Keep lessons specific.
- Keep lessons testable.
- Record only lessons supported by this repository's real tasks.
- Do not add motivational advice.

## Lessons

- If a task requests files under root `.agent/`, create or update them there even if legacy `agents/` files already exist.
- When defining local operating context, mark canonical vs legacy agent paths explicitly; otherwise the next task will split policy across both trees again.
- For MKS contract docs, state only behavior that is documented or directly visible in the current repository; unresolved areas must be labeled `Design in progress`.
- If `git diff` appears empty during an agent-doc task, check whether the affected files are untracked before assuming nothing changed.
- During GC review, search for registries allocated outside the GC graph that store `Environment *` or `RuntimeValue`; in MKS these side tables are a primary source of stale lifetime bugs.
- When constructing GC-managed arrays incrementally, do not wait until the end to expose element reachability; either root each live element or advance the array's visible count as elements become live.
- For semantic-contract tasks in MKS, document only the narrow behavior directly supported by runtime operators and tests; if a literal exists in the surface language, that does not automatically imply broad arithmetic or comparison semantics.
- For feature-contract tasks around side-effect systems like `watch` and `extend`, freeze only the trigger/registration surface that is directly exercised by current runtime call paths; leave propagation, conflict ordering, and module interaction out of the stable contract until they are explicitly tested.
- For MKS semantic docs, a runtime enum/value kind is not enough to claim a stable surface literal; parser-recognition and source-level tests must exist before documenting a token like `null` as user-available syntax.
- For release/process docs in MKS, separate repository readiness, language stability, and runtime/ABI stability; collapsing them into one “release” label overstates maturity and creates false compatibility promises.
- When a language-surface change affects `build/mks`, wait for the full build/link step to finish before trusting `./tests.sh`; otherwise black-box failures may come from an older binary while unit tests are already running the new one.
- For repository demo apps that use `std.fs` before a stable `std.path` layer exists, prefer explicit repo-relative data paths so the example runs deterministically from the repository root.
- For side-effecting module features like `extend`, test duplicate registration and repeated import separately; module-cache load-once behavior is not доказан by a plain duplicate declaration test inside one source.
- If a runtime object must survive for the full interpreter context lifetime, do not retain it through the temporary GC env-root stack; use persistent pin/unpin primitives and test both retention across `gc_collect` and cleanup on context disposal.
- Before changing lexer/parser for a requested surface form in MKS, inspect whether the parser already composes the syntax from existing keywords; if it does, lock it with tests/docs instead of inventing a new AST/token path.
- When promoting a fake stdlib concept into a core type in MKS, treat runtime printing, `String`/`Int`, native std modules, and black-box expected outputs as one migration unit; otherwise the repository ends up with mixed surface semantics (`true/false` in one layer, `1/0` in another).
