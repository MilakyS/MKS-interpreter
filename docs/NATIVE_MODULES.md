# Native Module Authoring Guide

Native modules are compiled C libraries (`.so` files) that extend the MKS interpreter with performance-critical or system-level functionality. They are loaded dynamically via `using native.X` syntax.

## Overview

A native module is a shared library that exports:
1. An initialization function `mks_module_init_X()` that populates the module's exports
2. An ABI version function `mks_module_abi_version()` for compatibility checking

Once loaded, the module's exports are available in the MKS namespace.

## Required Symbols

Every native module must export exactly two C functions:

### `mks_module_init_X(RuntimeValue exports, Environment *module_env)`

The initialization function called when the module is first imported.

- **Arguments:**
  - `exports`: An empty module object (dict-like) that your code should populate with functions or constants
  - `module_env`: The module's execution environment (rarely needed, but available for special use cases)

- **Example:**
  ```c
  void mks_module_init_math_ex(RuntimeValue exports, Environment *module_env) {
      MKS_EXPORT_NATIVE(exports, "add", native_add);
      MKS_EXPORT_NATIVE(exports, "multiply", native_multiply);
  }
  ```

### `int mks_module_abi_version(void)`

Returns the ABI version your module was built against. Must return `MKS_MODULE_ABI_VERSION`.

- **Example:**
  ```c
  int mks_module_abi_version(void) {
      return MKS_MODULE_ABI_VERSION;
  }
  ```

Or use the provided macro:
```c
MKS_MODULE_DECLARE_ABI
```

## ABI Versioning

**Current version:** `MKS_MODULE_ABI_VERSION = 1`

The ABI version ensures that old compiled modules are rejected if they are incompatible with the current interpreter version.

### When ABI Version Bumps

The ABI version increments when:
- The layout or fields of `RuntimeValue` change
- The layout or fields of `Environment` change
- Function signatures in `mks_module.h` change
- Symbol-resolution or calling conventions change

### What v1 Guarantees

When you build a module against `MKS_MODULE_ABI_VERSION = 1`, you are guaranteed:
- `RuntimeValue` struct layout remains unchanged
- `Environment *` pointer semantics remain unchanged
- The `MKS_EXPORT_NATIVE` macro works correctly
- The module can call `module_bind_native()` and other `mks_module.h` APIs

## Full Example: Math Module

Create a file `math_ex.c`:

```c
#include "mks_module.h"
#include <math.h>

static RuntimeValue native_sqrt(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    if (arg_count < 1) {
        return runtime_value_null();
    }
    double val = to_number(args[0]);
    return make_number(sqrt(val));
}

static RuntimeValue native_pow(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    if (arg_count < 2) {
        return runtime_value_null();
    }
    double base = to_number(args[0]);
    double exp = to_number(args[1]);
    return make_number(pow(base, exp));
}

MKS_MODULE_INIT(math_ex) {
    MKS_EXPORT_NATIVE(exports, "sqrt", native_sqrt);
    MKS_EXPORT_NATIVE(exports, "pow", native_pow);
}

MKS_MODULE_DECLARE_ABI
```

## Build Instructions

Compile your module with position-independent code and link as a shared library:

```bash
gcc -shared -fPIC -o math_ex.so math_ex.c -lm
```

Or, for a more robust build:

```bash
gcc -shared -fPIC -Wall -Wextra \
    -I/path/to/mks/headers \
    -o math_ex.so math_ex.c -lm
```

If the MKS headers are installed system-wide:

```bash
gcc -shared -fPIC -o math_ex.so math_ex.c -lm
```

## Module Placement

MKS searches for native modules in the following order:

1. `./mks_modules/<name>/<name>.so` (current directory or script's directory)
2. Parent directories up to 8 levels up: `../mks_modules/<name>/<name>.so`
3. System paths:
   - `/usr/local/lib/mks/<name>.so`
   - `/usr/lib/mks/<name>.so`

**Example directory structure:**

```
my_project/
  mks_modules/
    math_ex/
      math_ex.so
      README.md
  scripts/
    main.mks
```

In `main.mks`:
```mks
using native.math_ex as m;
Writeln(m.sqrt(16.0));  // prints 4
```

## Safety Contract

### Allowed During `init()`

Your `mks_module_init_X()` function may:
- Call `MKS_EXPORT_NATIVE()` to bind functions
- Read from `module_env` (if needed for state)
- Allocate memory (the interpreter manages cleanup)
- Call any interpreter API declared in `mks_module.h`

### Disallowed During `init()`

Your `mks_module_init_X()` function **must not**:
- Call `runtime_error()` or cause a panic (cleanup would leak resources)
  - Instead, return early if initialization fails and document the limitation
- Call MKS code that evaluates user scripts (Eval/VM integration)
- Assume the garbage collector is in any particular state
- Store raw pointers to `RuntimeValue` or `Environment` structures in global state (use the pinning API)

### Panic Safety

If your `init()` calls `runtime_error()`, the interpreter's error handler will:
1. Catch the longjmp
2. Close the `.so` handle
3. Mark the module as "failed" in the cache
4. Propagate the error to the caller

Therefore, **do not use `runtime_error()` for expected conditions**. Return `NULL` or handle errors gracefully instead.

## Common Pitfalls

### 1. Forgetting ABI Version

**Problem:** Module loads but crashes with undefined symbol error.

**Fix:** Always export `mks_module_abi_version()` or use the `MKS_MODULE_DECLARE_ABI` macro.

### 2. Wrong Function Signature

**Problem:** Module compiles but init function is not called.

**Fix:** Check that the function is named exactly `mks_module_init_<name>`, where `<name>` matches the module name in the import.

```mks
using native.math_ex as m;  // Expects: mks_module_init_math_ex
```

### 3. Path Traversal Attempts

**Problem:** Trying to load a module named `../../evil`.

**Fix:** Module names are validated; they must not contain `/`, `\`, or `..`. This is a compile-time error.

```mks
using native."../../evil" as x;  // ERROR: invalid native module name
```

### 4. Storing Unmanaged Pointers

**Problem:** You store a `RuntimeValue*` in a static and access it later.

**Fix:** Use the pinning API if you must persist values across calls:
```c
gc_pin_root(&my_stored_value);
```

And unpin before the module is unloaded (though currently modules are never unloaded).

### 5. Module Not Found

**Problem:** `using native.X` fails with "cannot find native.X".

**Fix:** Check:
- The `.so` file exists in one of the search paths
- The file is readable and actually a valid ELF shared library
- The module name in the import matches the base name (without `.so`)

### 6. Calling External C Libraries

**Problem:** Your module links against `libcurl` but the `.so` fails to load with undefined symbol error.

**Fix:** Link your module with the required libraries:
```bash
gcc -shared -fPIC -o http.so http.c -lcurl
```

## Version Compatibility

If you build a module against MKS 0.2 with `ABI_VERSION = 1`, and then update to MKS 0.3 with a different `ABI_VERSION = 2`, the old module will be rejected at load time with a clear error message:

```
native module 'math_ex' ABI mismatch: expected 2, got 1
```

You will need to rebuild the module against the new interpreter.

---

For further questions or to report issues, consult the MKS repository or documentation.
