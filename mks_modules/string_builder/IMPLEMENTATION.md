# StringBuilder Implementation Notes

## Overview

The StringBuilder module is a loadable native MKS module implemented in C that provides efficient string building with O(n) complexity.

## Architecture

### File Structure
```
string_builder.c
├── struct MksStringBuilder
│   ├── char *data
│   ├── size_t len
│   └── size_t cap
├── Helper functions
│   ├── sb_grow()
│   ├── sb_append_cstr()
│   └── value_to_string()
├── Native function implementations
│   ├── native_builder()
│   ├── native_append()
│   ├── native_append_raw()
│   ├── native_append_line()
│   ├── native_clear()
│   ├── native_len()
│   ├── native_capacity()
│   ├── native_to_string()
│   └── native_reserve()
├── Module initialization
│   └── mks_module_init_string_builder()
└── ABI version
    └── mks_module_abi_version()
```

### Memory Management

**Internal Structure (`MksStringBuilder`)**
```c
typedef struct {
    char *data;     /* heap-allocated buffer, always null-terminated */
    size_t len;     /* current string length in bytes */
    size_t cap;     /* allocated buffer capacity in bytes */
} MksStringBuilder;
```

**GC Integration**
- The C struct is allocated on the heap via `malloc()`
- A pointer to the struct is stored in a hidden field (`__ptr`) of a builder object
- The builder object itself is a proper MKS `Object` (Environment-based)
- When the MKS object is garbage collected, the C struct is freed manually (not automatic)
- The GC cannot directly free the C pointer without a finalizer; memory cleanup is implicit

### Buffer Growth Strategy

When `len + needed > cap`:
1. Calculate new capacity: `new_cap = max(64, cap * 2)` or double
2. If still insufficient, keep doubling: `while (new_cap < required) new_cap *= 2`
3. Reallocate buffer: `realloc(data, new_cap)`
4. Update capacity field

This gives amortized O(1) append with exponential growth.

## Integration Points

### Module Loading (`Runtime/module.c`)

**Change: RTLD_GLOBAL Flag**
- Modified `load_native_dlopen()` to use `RTLD_LAZY | RTLD_GLOBAL` instead of `RTLD_LOCAL`
- This allows the loaded .so to access symbols from the main executable (e.g., `module_bind_native()`)
- Critical for native modules that call interpreter functions

### Executable Building (`CMakeLists.txt`)

**Change: Symbol Export**
- Added `target_link_options(mks PRIVATE -rdynamic)` to export all symbols
- Ensures the main executable's symbols are available to dynamically loaded .so files
- Required for symbol resolution when modules call interpreter functions

### Module Entry Point

**Required Exports:**
1. `mks_module_abi_version()` — Returns `MKS_MODULE_ABI_VERSION (1)`
2. `mks_module_init_string_builder()` — Initializes and registers all functions

## Type Conversions

**`value_to_string()` conversion rules:**
- `VAL_NULL` → `"null"`
- `VAL_BOOL` → `"true"` or `"false"`
- `VAL_INT` → Decimal string representation
- `VAL_FLOAT` → Decimal string with `%g` format
- `VAL_STRING` → Direct string data
- Other types → `"[object]"`

## Function Signatures

All native functions follow the MKS native function convention:
```c
RuntimeValue native_*(MKSContext *ctx, const RuntimeValue *args, int arg_count)
```

**Return Chaining:**
- `append()`, `append_raw()`, `append_line()`, `clear()`, `reserve()` return the builder for chaining
- `len()`, `capacity()` return integer values
- `to_string()` returns a new string value

## Error Handling

All errors are fatal:
- Out of memory → `runtime_error("StringBuilder: out of memory")`
- Type mismatches → `runtime_error("append_raw: second argument must be a string")`
- Corrupted objects → `runtime_error("builder: corrupted builder object")`

Errors trigger MKS's error handling (longjmp) and propagate to user code.

## GC Safety

The module handles GC safety through:

1. **Object-based storage:** The C struct pointer is hidden in an object field
2. **No raw pointers exposed:** Users see the builder as an opaque object
3. **Environment binding:** Uses `env_set_fast()` with hashed field name
4. **No GC interactions:** The C code doesn't call GC functions directly

**Potential limitation:** When a builder object is garbage collected, the C struct's heap memory (`data` buffer) is not automatically freed. This is acceptable because:
- Builders are typically short-lived
- The interpreter's memory management handles cleanup on exit
- Manual free would require a finalizer callback (not implemented)

## Compilation

**Dependencies:**
- `mks_module.h` — Module API definitions
- `env/env.h` — Environment API for object storage
- `Utils/hash.h` — Hash function for field names
- Standard C library

**Compilation:**
```bash
gcc -shared -fPIC string_builder.c -o string_builder.so
```

**Placement:**
```
mks_modules/string_builder/string_builder.so
```

## Testing

**Test Coverage:**
- Basic builder creation (`builder()`)
- String appending with type conversion (`append()`)
- Raw string appending (`append_raw()`)
- Newline appending (`append_line()`)
- Clear and reuse (`clear()`)
- Capacity/length queries (`len()`, `capacity()`)
- Buffer output (`to_string()`)
- Capacity reservation (`reserve()`)
- Operation chaining
- Mixed types (int, float, bool, string)

**Example Tests:**
- `test_string_builder.mks` — Basic API tests
- `test_string_builder_advanced.mks` — Chaining, HTML building, formatting
- `example_string_builder_perf.mks` — Performance example with CSV/JSON building

## Performance Characteristics

| Operation | Complexity | Notes |
|-----------|-----------|-------|
| `append()` | O(1) amortized | Exponential growth averages cost |
| `append_raw()` | O(1) amortized | Same as append() |
| `append_line()` | O(1) amortized | String concat + newline |
| `clear()` | O(1) | Only sets len=0; doesn't deallocate |
| `len()` | O(1) | Field lookup |
| `capacity()` | O(1) | Field lookup |
| `to_string()` | O(n) | String creation and copy |
| `reserve()` | O(n) for growth | Realloc; only if needed |

**Space Complexity:**
- `O(n)` where n is the final string length
- Capacity grows exponentially, so spare = O(n) at most

## Known Limitations

1. **Single-threaded:** Not safe to share builders between concurrent execution contexts
2. **No shrinking:** Capacity only increases, never decreases
3. **No finalizer:** Memory cleanup relies on process termination
4. **Method calls:** Must call as `sb.append(builder, value)`, not `builder.append(value)`

## Future Enhancements (Not Implemented)

- Explicit `destroy()` function for early cleanup
- Unicode support (currently assumes UTF-8)
- Substring operations
- Pattern matching and replacement
- Iterator protocol for streaming
- Lazy string evaluation

## Compatibility Notes

- **ABI Version:** 1
- **MKS Version:** 0.2+
- **Platform:** Linux x86-64 (requires dlfcn.h)
- **Runtime:** Requires -rdynamic flag on main executable
