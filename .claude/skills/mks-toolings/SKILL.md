---
name: mks-tooling-release-engineer
description: Maintain MKS tooling, tests, docs, CI, release flow, package metadata, CLI behavior, and developer experience without breaking project conventions.
---

# MKS Tooling & Release Engineer

## When to Use

Use this skill when working on:

- CLI flags and user-facing commands
- `tests.sh`, golden tests, unit tests, regression tests
- CMake/build configuration
- GitHub Actions / CI
- release scripts and `dist/` packaging
- `.mkspkg` / package tooling
- AUR / `PKGBUILD` / `.SRCINFO`
- documentation sync
- website docs/examples
- project metadata
- installation flow
- developer scripts

## Goal

Keep MKS easy to:

- build
- test
- package
- install
- document
- release
- debug

without changing language/runtime behavior accidentally.

## Core Rule

Tooling changes must be boring, reproducible, and safe.

Do not mix tooling cleanup with runtime/language behavior changes unless explicitly required.

---

# Required Checks

## 1. Build System

When changing CMake/build files, verify:

- clean build works
- release build works
- debug build works
- ASan/UBSan options still work if present
- stdlib/native modules are included
- install rules include binary + stdlib + docs if expected
- generated artifacts do not depend on local machine paths

Required commands when relevant:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
2. Tests

When changing tests or behavior, verify:

golden tests still pass
new feature has a regression test
error output tests are stable
VM/tree-walk behavior is not accidentally diverging
tests are deterministic
tests do not depend on absolute paths, local usernames, or terminal size unless isolated

Preferred test categories:

positive behavior
negative/error behavior
regression
VM mode
tree-walk mode
GC stress, if allocation-heavy
3. CLI Behavior

When changing CLI flags, verify:

help output is updated
unknown flag errors are clear
flag combinations behave predictably
default behavior remains backwards-compatible
scripts using old flags do not silently break

Required docs update if user-facing.

4. Release Packaging

When touching release scripts, verify package contains:

bin/mks
std/
README.md
LICENSE
docs or reference files, if release expects them

Check:

version argument is required
release archive name is stable
build type is Release
tests run before packaging
stale dist files do not pollute new release
permissions are sane
5. Package Manager / mkspkg

When changing package tooling, verify:

package install path is deterministic
package cache does not corrupt existing modules
package namespace resolution is explicit
local package and std package precedence is documented
error messages tell user what to fix
lock/cache metadata remains parseable

Never silently fall back to unsafe paths.

6. CI

When changing GitHub Actions, verify:

workflow runs on intended branches
required dependencies are installed
cache does not hide build failures
tests run in clean environment
artifacts are uploaded only when useful
matrix jobs are not excessive
failure output is readable

Preferred CI checks:

configure
build
tests
asan/ubsan, if available
package smoke test
7. Documentation Sync

When changing user-facing behavior, update relevant docs:

README
docs/reference
website docs
examples
changelog/release notes

Docs must match actual syntax.

Bad:

return { name: "x" }

if object literals are not supported.

Docs should prefer examples that currently run.

8. Website / Examples

When changing website examples:

verify examples use valid MKS syntax
avoid future syntax unless clearly marked experimental
do not document unavailable stdlib APIs
keep examples small enough to copy-paste
mention VM-only or tree-walk-only behavior if relevant
9. AUR / Linux Packaging

When touching AUR files, verify:

pkgver() is deterministic
source URL points to correct repository
build uses CMake release mode
install step respects $pkgdir
.SRCINFO matches PKGBUILD
license and dependencies are accurate

Never require sudo inside package().

10. Developer Experience

Prefer scripts that:

fail fast
print clear next steps
avoid hidden global state
work from repo root
handle missing dependencies clearly
do not destroy user files
Red Flags

Stop and inspect if you see:

tooling change mixed with runtime rewrite
docs showing unsupported syntax
CI that passes without running tests
release package missing std/
script using absolute local paths
package step using sudo
tests depending on terminal size without fallback
generated files committed accidentally
.SRCINFO out of sync
install path shadowing stdlib unsafely
silent fallback after failed package resolution
Output Format

Use this format when reviewing or planning tooling work:

[MKS TOOLING REVIEW]

Goal:
Area:
User-facing change:
Files likely touched:

Build impact:
Test impact:
Docs impact:
Release/package impact:
Compatibility risk:

Required checks:
- build:
- tests:
- docs:
- packaging:
- CI:

Minimal patch plan:
1.
2.
3.

Do not change:
Final Rule

Tooling exists to make MKS easier to trust.

A tooling patch is good only if it makes build/test/release/docs more reproducible without accidentally changing language semantics.
