# 🐒 MKS 
Small experimental scripting language + interpreter in C.

## Contents
- [Feature overview](#feature-overview)
- [Hello, MKS](#hello-mks)
- [Language tour](#language-tour)
- [Entities & extend](#entities--extend)
- [Imports & stdlib](#imports--stdlib)
- [Watch / defer](#watch--defer)
- [CLI](#cli)
- [Repo layout](#repo-layout)
- [Docs](#docs)

## Feature overview
- Expressions: numbers, strings, arrays, indexing.
- Control flow: `if/else`, `while`, `for`, `repeat`, `break`, `continue`.
- Functions & recursion: `fnc name(args) -> ... <-`, `return`.
- Swap operator: `a <--> b;` (fast in-place).
- Defer & watch/on change.
- Entities (lightweight objects) + methods; extend built-ins (array/string).
- Namespace imports with std/package modules.
- Friendly errors with hints; recursion-depth guard.
- Simple mark/sweep GC; sanitizer-friendly build.

## Hello, MKS
```mks
using std.math as math;

var x =: 10;
watch x;
on change x -> Writeln("x changed"); <-

x =: math.square(x);     // triggers watcher
Writeln("result:", x);
```


## Language tour
Variables & swap
```mks
var a =: 1;
var b =: 2;
a <--> b;          // swap
```

Control flow
```mks
if (a > 0) -> Writeln("positive"); <-
while (a < 5) -> a =: a + 1; <-
for (var i =: 0; i < 3; i =: i + 1) -> Writeln(i); <-
repeat 3 -> Writeln("ping"); <-
repeat i in 3 -> Writeln(i); <-
```

Functions
```mks
fnc add(a, b) ->
    return a + b;
<-
Writeln(add(2, 3));
```

Tests
```mks
test "math add" ->
    expect(1 + 1 ?= 2);
<-
```

## Entities & extend
```mks
entity User(name) ->
    init -> self.name =: name; <-
    method hi() -> Writeln("Hi ", self.name); <-
<-

var u =: User("Ann");
u.hi();

extend array ->
    method sum() ->
        var s =: 0; var i =: 0;
        while (i < self.len()) -> s =: s + self[i]; i =: i + 1; <-
        return s;
    <-
<-

Writeln([1,2,3].sum()); // 6
```

## Imports & stdlib
```mks
using "./path/to/file";         // relative file
using std.math as math;         // std namespace
using mypkg.tools.strings as s; // package module
```
`using` always binds a namespace object into the current scope. It does not copy exports into the scope.

Resolution order:
- relative paths like `./` and `../`
- std modules like `std.math`
- package imports resolved from the nearest `mks.toml`

Imported modules load declarations once. Top-level executable statements are skipped during import; put reusable behavior inside exported functions.

`std.math`:
```mks
using std.math as math;
Writeln(math.abs(-5));
Writeln(math.square(4));
Writeln(math.min(10, 3));
Writeln(math.max(10, 3));
```

Minimal package layout:
```text
my-app/
  mks.toml
  src/main.mks
  packages/
    cool.lib/
      mks.toml
      src/main.mks
      src/http/client.mks
```

## Watch / defer
```mks
watch x;
on change x -> Writeln("x changed to ", x); <-

defer -> Writeln("leaving scope"); <-
```
`on change` must be registered before the assignment you want to observe.

## CLI
```
mks <file.mks>   # run file
mks --repl       # interactive REPL
mks --version
mks --help
```

## Install
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

User-local install:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
cmake --install build
```

Installed layout:
- `bin/mks`
- `share/mks/std/...`

Runtime stdlib lookup order:
- `MKS_STD_PATH`
- install-relative data dir near the executable
- configured install data dir
- common system paths


## Repo layout
- `Lexer/` – tokens
- `Parser/` – AST + parsing
- `Eval/` – evaluation
- `Runtime/` – values, operators, modules, errors
- `env/` – variable scopes
- `GC/` – garbage collector
- `std/` – bundled stdlib (`std/math.mks`)
- `examples/` – runnable samples
- `docs/` – reference / guides
- `tests/` – golden tests (`./tests.sh`)

## Docs
- `docs/REFERENCE.md` — quick reference (syntax & examples).
- `docs/USER_GUIDE.md` — step-by-step guide (entities, extend, watch/defer, repeat, swap).

## Status
Active, experimental. APIs may change. Friendly errors should point to the fix; if not, please open an issue.

## Badges
![Build](https://img.shields.io/badge/build-passing-brightgreen)
![License](https://img.shields.io/badge/license-APACHE-blue)
![Status](https://img.shields.io/badge/status-experimental-orange)
