# MKS Packages

Packages in MKS are namespace modules. `using` imports a namespace object; it
does not copy exported names into the current scope.

The package system has two goals:
- keep source packages simple while developing;
- distribute packages as a single `.mkspkg` file.

## Quick Start

Create a package:

```bash
mkdir cool.lib
cd cool.lib
mks pkg init cool.lib
mks pkg build
```

`mks pkg build` creates:

```text
dist/cool.lib-0.1.0.mkspkg
dist/cool.lib.mkspkg
```

Use the canonical file name for local installs:

```text
app/
  main.mks
  cool.lib.mkspkg
```

Then import it:

```mks
using cool.lib as cool;
Writeln(cool.hello());
```

No install command is required for this preview. A package artifact is just a
file with the canonical name `<package>.mkspkg`.

## Package Layout

Recommended source layout:

```text
cool.lib/
  package.pkg
  src/
    lib.mks
    http/client.mks
  README.md
  LICENSE
```

Application layout with flat package artifacts:

```text
app/
  main.mks
  cool.lib.mkspkg
  helper.text.mkspkg
```

Application layout with a package cache:

```text
app/
  main.mks
  mks_modules/
    cool.lib.mkspkg
    helper.text.mkspkg
```

Both layouts are valid. Flat artifacts are the shortest path for small projects;
`mks_modules/` is useful when the project has many installed packages.

## package.pkg

`package.pkg` is a declarative manifest. It is not executable MKS code.

```mks
package "cool.lib";
version "0.1.0";
lib "src/lib.mks";
main "src/main.mks";
```

Fields:
- `package` is required and defines the import id.
- `version` is optional metadata and is used in the versioned build artifact.
- `lib` is optional and defaults to `src/lib.mks`; it is used by `using cool.lib`.
- `main` is optional and records the package application entrypoint.

`lib` and `main` are package-relative paths. The default layout uses `src/`, but
custom paths are supported:

```mks
package "custom.lib";
version "0.1.0";
lib "custom/root.mks";
```

`mks pkg build` includes declared `lib` and `main` files even when they are
outside `src/`.

## Imports

```mks
using std.math as math;
using "./local/file" as local;
using cool.lib as cool;
using cool.lib.http.client as client;
```

Resolution order:
1. `std.*` resolves from the standard library and cannot be shadowed.
2. `./...` and `../...` resolve relative to the current file.
3. The current package name resolves from the nearest `package.pkg` or current
   `.mkspkg`.
4. External packages resolve from flat `.mkspkg` files, `mks_modules/`, and
   legacy `packages/`.
5. Dotted ids that are not packages fall back to file-style module resolution.

External package lookup for `using cool.lib as cool;` checks:

```text
<root>/cool.lib.mkspkg
<root>/cool.lib/
<root>/mks_modules/cool.lib.mkspkg
<root>/mks_modules/cool.lib/
<root>/packages/cool.lib/
```

`<root>` is the current package root, or the directory of the running script when
there is no package manifest.

When code runs from an installed archive, sibling packages are searched from the
archive directory too:

```text
app/
  main.mks
  uses.dep.mkspkg
  dep.lib.mkspkg
```

Inside `uses.dep.mkspkg`, this works:

```mks
using dep.lib as dep;
```

The same is true under `mks_modules/`:

```text
app/
  main.mks
  mks_modules/
    uses.dep.mkspkg
    dep.lib.mkspkg
```

## Submodules

Package root import:

```mks
using cool.lib as cool;
```

Loads:

```text
lib "..." from package.pkg
src/lib.mks
src/main.mks
```

Package submodule import:

```mks
using cool.lib.http.client as client;
```

Loads:

```text
src/http/client.mks
```

Relative imports inside `.mkspkg` stay inside the same archive:

```mks
using "./http/client" as client;
```

From `cool.lib.mkspkg::src/lib.mks`, that resolves to:

```text
cool.lib.mkspkg::src/http/client.mks
```

## .mkspkg Format

`.mkspkg` is a single uncompressed archive file.

Artifact contents:

```text
package.pkg
src/**/*.mks
README.md      optional
LICENSE        optional
custom lib/main paths from package.pkg
```

Not included:
- tests;
- docs directories;
- build outputs;
- `.git`;
- nested `mks_modules`.

Archive format v1:
- magic header: `MKSPKG1\n`
- repeated entries: `path_len u32`, `size u64`, path bytes, content bytes
- terminator: `path_len = 0`
- no compression yet

## Build

```bash
mks pkg build
```

For `package "cool.lib"; version "0.1.0";`, build creates:

```text
dist/cool.lib-0.1.0.mkspkg
dist/cool.lib.mkspkg
```

The versioned file is useful for publishing and release archives. The canonical
file is useful for local installs because the runtime looks for
`<package>.mkspkg`.

Local flat install:

```bash
cp dist/cool.lib.mkspkg ../app/
```

Local cache install:

```bash
mkdir -p ../app/mks_modules
cp dist/cool.lib.mkspkg ../app/mks_modules/
```

Both imports are identical from MKS code:

```mks
using cool.lib as cool;
```

## Bundled Package Example: GGui

The repository includes `packages/ggui`, a TUI package built on `std.tty`.

Build it:

```bash
mks pkg build packages/ggui
```

Install it flat next to an app:

```bash
cp packages/ggui/dist/ggui.mkspkg app/
```

Use it:

```mks
using ggui as ui;

ui.panel(2, 2, 42, 7, "GGui", "Русский and English text wrap safely.");
ui.flush();
```

See `docs/GGUI.md` for widget-level docs.

## Exports

Only exported symbols are visible through the imported namespace:

```mks
var hidden =: 1;
export var VERSION =: "0.1.0";

export fnc square(x) ->
    return x * x;
<-
```

Usage:

```mks
using cool.lib as cool;

Writeln(cool.VERSION);
Writeln(cool.square(4));
```

When imported, a module loads declarations only: `using`, `var`, `fnc`,
`entity`, `extend`, and `export`. Top-level executable statements such as
`Writeln`, loops, and ordinary calls are skipped during import. Put reusable
work inside exported functions.

## Compatibility

The older preview layout still works:

```text
app/
  mks.toml
  src/main.mks
  packages/cool.lib/
    mks.toml
    src/main.mks
```

Legacy behavior:
- `mks.toml` may provide `name = "app.name"`.
- package root imports load `src/main.mks`.
- external packages may be found under `packages/<name>/`.

New packages should prefer `package.pkg`, `src/lib.mks`, and single-file
`.mkspkg` artifacts.
