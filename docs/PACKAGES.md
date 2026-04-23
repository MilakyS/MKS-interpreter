# MKS Packages

Packages in MKS start with one simple rule: `using` imports a namespace module; it does not copy exported names into the current scope.

## Manifest

The package root contains `mks.toml`:

```toml
name = "my.app"
version = "0.1.0"
entry = "src/main.mks"
```

The runtime currently uses `mks.toml` to find the package root and the current package name.

## Layout

```text
my-app/
  mks.toml
  src/
    main.mks
    util/strings.mks
  packages/
    cool.lib/
      mks.toml
      src/
        main.mks
        http/client.mks
```

## Imports

```mks
using "./util/strings" as strings;
using my.app.util.strings as strings2;
using cool.lib as lib;
using cool.lib.http.client as client;
using std.math as math;
```

Resolution rules:
- `./...` and `../...` — relative file imports
- `std.*` — stdlib
- other dotted ids — package imports

## Package Resolution

The runtime searches upward from the current file for the nearest `mks.toml`.

If an import matches the current package name from `mks.toml`, it resolves to:
- `src/main.mks` for the package root
- `src/<submodule>.mks` for submodules

If an import points to an external package, it is searched in:

```text
<current-package-root>/packages/<package-name>/mks.toml
```

and then loads:
- `src/main.mks`
- or `src/<submodule>.mks`

## Stdlib Installation

The standard library is installed together with the binary through CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build
```

The resulting layout is:

```text
<prefix>/bin/mks
<prefix>/share/mks/std/...
```

At startup, `mks` searches for the stdlib in this order:
- `MKS_STD_PATH`
- install-relative path next to the binary
- configured install data directory
- system paths

## Exports

Supported forms:

```mks
export var PI =: 3.14;

export fnc square(x) ->
    return x * x;
<-
```

Usage:

```mks
using cool.lib as lib;
Writeln(lib.square(4));
```

When imported, a module loads declarations only (`using`, `var`, `fnc`, `entity`, `extend`, `export`). Top-level calls, `Writeln`, loops, and other executable code are skipped during import; reusable code should be moved into exported functions.
