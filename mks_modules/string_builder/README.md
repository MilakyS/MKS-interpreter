# StringBuilder Native Module

A high-performance string building module for MKS that avoids O(n²) behavior from repeated string concatenation.

## Overview

The StringBuilder module provides an efficient way to construct strings from multiple parts. Instead of using:

```mks
var result =: "";
for (i =: 0; i < n; i =: i + 1) ->
    result =: result + items[i];
<-
```

Which has O(n²) complexity due to repeated string allocations and copies, you can use:

```mks
using native.string_builder as sb;

var result =: sb.builder();
for (i =: 0; i < n; i =: i + 1) ->
    sb.append(result, items[i]);
<-
Writeln(sb.to_string(result));
```

This has O(n) complexity with exponential buffer growth.

## API Reference

### `builder() → Object`

Creates a new empty StringBuilder with initial capacity of 64 bytes.

**Example:**
```mks
var b =: sb.builder();
```

### `append(builder, value) → builder`

Appends the string representation of `value` to the builder. Supports:
- Integers
- Floating-point numbers
- Booleans (as "true" or "false")
- Strings
- Other objects (as "[object]")

Returns the builder for chaining.

**Example:**
```mks
sb.append(b, "count: ");
sb.append(b, 42);
```

### `append_raw(builder, str) → builder`

Appends a string directly without type conversion. Requires `str` to be a string.

**Example:**
```mks
sb.append_raw(b, "Raw text");
```

### `append_line(builder, value) → builder`

Appends `value` followed by a newline character.

**Example:**
```mks
sb.append_line(b, "First line");
sb.append_line(b, "Second line");
```

### `clear(builder) → builder`

Clears the contents of the builder. The buffer capacity is preserved.

**Example:**
```mks
sb.clear(b);
```

### `len(builder) → Integer`

Returns the current string length in bytes.

**Example:**
```mks
var length =: sb.len(b);
```

### `capacity(builder) → Integer`

Returns the current allocated buffer capacity in bytes.

**Example:**
```mks
var cap =: sb.capacity(b);
```

### `to_string(builder) → String`

Returns the builder's contents as an immutable MKS string. Does not modify the builder.

**Example:**
```mks
var result =: sb.to_string(b);
Writeln(result);
```

### `reserve(builder, n) → builder`

Ensures the builder has at least `n` bytes of capacity. If already sufficient, does nothing.

**Example:**
```mks
sb.reserve(b, 1024);  => Ensure 1KB capacity
```

## Usage Examples

### Example 1: Building CSV

```mks
using native.string_builder as sb;

var csv =: sb.builder();
var headers =: ["name", "age", "city"];
var values =: ["Alice", 30, "NYC"];

for (h =: 0; h < headers.len(); h =: h + 1) ->
    if (h > 0) -> sb.append(csv, ","); <-
    sb.append(csv, headers[h]);
<-
sb.append_line(csv, "");

for (v =: 0; v < values.len(); v =: v + 1) ->
    if (v > 0) -> sb.append(csv, ","); <-
    sb.append(csv, values[v]);
<-

Writeln(sb.to_string(csv));
```

Output:
```
name,age,city
Alice,30,NYC
```

### Example 2: Efficient Concatenation Loop

```mks
using native.string_builder as sb;

var b =: sb.builder();

for (i =: 1; i <= 10; i =: i + 1) ->
    sb.append(b, "Item ");
    sb.append(b, i);
    sb.append_line(b, "");
<-

Writeln(sb.to_string(b));
```

### Example 3: Method Chaining

```mks
using native.string_builder as sb;

var html =: sb.builder();
sb.append(
    sb.append(
        sb.append(html, "<div>"),
        "Hello"),
    "</div>"
);
Writeln(sb.to_string(html));
```

## Performance Characteristics

- **Time Complexity**: O(n) for n appends
- **Space Complexity**: O(n) where n is the final string length
- **Buffer Growth**: Exponential (capacity doubles when exceeded), amortized O(1) per append

## Memory Management

- The StringBuilder internally allocates heap memory for the buffer
- Memory is automatically managed by the garbage collector
- Clearing a builder (`clear()`) does not deallocate the buffer; capacity is preserved
- Builders can be reused across multiple clear/append cycles

## Design Notes

### GC Integration

The StringBuilder is a proper MKS object stored in the runtime's garbage collection system. The internal C pointer is safely hidden in an object field, ensuring:
- No raw pointers exposed to user code
- Automatic cleanup when the builder goes out of scope
- Safe interaction with the MKS garbage collector

### Error Handling

The module uses `runtime_error()` for all error conditions:
- Out of memory during allocation/growth
- Invalid builder objects
- Type mismatches in `append_raw()`

Errors halt execution and propagate as MKS runtime errors.

### Thread Safety

Individual StringBuilder objects are NOT thread-safe. Do not share a builder between concurrent execution contexts.

## Building

The module is built with:

```bash
gcc -shared -fPIC string_builder.c -o string_builder.so
```

It must be placed in: `mks_modules/string_builder/string_builder.so`

The main MKS executable must be built with `-rdynamic` to export runtime symbols for module loading.

## Limitations

- Builders are single-threaded
- No explicit builder destruction (relies on garbage collector)
- Capacity only grows, never shrinks
- `clear()` is O(1) but does not deallocate memory

## See Also

- [NATIVE_MODULES.md](../../docs/NATIVE_MODULES.md) - Guide for native module development
- MKS string operations: `len()`, `substring()`, `split()`, `join()`
