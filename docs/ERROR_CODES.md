# MKS Error Codes

MKS diagnostics are meant to be stable, searchable, and useful in tests, CI logs,
and documentation. Every diagnostic has a code, a category, a reason, a help
message, and usually an example/next step.

The public code format is:

```text
MKS-<subsystem><number>
```

Examples:

```text
MKS-S1001
MKS-R3001
MKS-M4001
```

The subsystem letter tells which part of the interpreter produced the error.
The number is split into a range and a stable per-error ID.

Example runtime output:

```text
[MKS Runtime Error]
code: MKS-R3001
kind: name resolution
error: Undefined variable 'missing_value'
reason: MKS tried to read or assign a name that is not visible in the current scope
 --> errorstest/02_undefined_variable.mks:1:14
   1 | var total =: missing_value + 1;
     |              ^~~~~~~~~~~~~
help: declare the variable before using it, or check the spelling/scope
example: var value =: 1; Writeln(value);
next: look for a missing 'var', a typo, or a variable declared inside another block
```

## Ranges

```text
MKS-L0000..L0999  Lexer diagnostics
MKS-S1000..S1999  Parser/syntax diagnostics
MKS-T2000..T2999  Static type diagnostics, reserved
MKS-R3000..R3999  Runtime value/control-flow diagnostics
MKS-M4000..M4999  Module/import/export diagnostics
MKS-G5000..G5999  GC/runtime-internal diagnostics, reserved
MKS-I9000..I9999  Internal compiler/interpreter diagnostics
```

Ranges are intentionally sparse. The gaps are for future diagnostics, so existing
codes do not need to move when a new error is added.

## Number Layout

The first digit of the number chooses the broad range:

```text
0xxx  Lexer
1xxx  Syntax/parser
2xxx  Static typing, reserved
3xxx  Runtime
4xxx  Module system
5xxx  GC/runtime internals, reserved
9xxx  Internal compiler/interpreter errors
```

Inside a range, the next digit can define a smaller group. For example runtime
uses these groups:

```text
R3000..R3099  Generic runtime and name resolution
R3100..R3199  Function/method calls
R3200..R3299  Indexing
R3300..R3399  Numeric operations
R3400..R3499  Object/property access
R3500..R3599  Type/operator mismatch
```

The final two digits are the concrete diagnostic number inside that group:

```text
xx00  Generic fallback for that group, if needed
xx01  First concrete diagnostic in that group
xx02  Second concrete diagnostic in that group
...
```

So `MKS-S1001` means:

```text
S     Syntax/parser subsystem
1000  Syntax diagnostic range
001   First concrete syntax diagnostic: unexpected token
```

`MKS-R3101` means:

```text
R     Runtime subsystem
3000  Runtime diagnostic range
100   Call-related group inside runtime
001   First concrete call diagnostic: wrong argument count
```

`MKS-R3301` means:

```text
R     Runtime subsystem
3000  Runtime diagnostic range
300   Numeric-operation group inside runtime
001   First concrete numeric diagnostic
```

So `R3301` is used instead of `R3302` because it is currently the first numeric
runtime diagnostic. `R3302` is reserved for the next numeric runtime diagnostic,
for example numeric overflow or invalid integer conversion.

## Current Codes

```text
MKS-L0001  Invalid lexer token

MKS-S1000  Generic syntax error
MKS-S1001  Unexpected token
MKS-S1002  Expected token missing
MKS-S1003  Invalid assignment target

MKS-R3000  Generic runtime error
MKS-R3001  Undefined name
MKS-R3101  Wrong function/method argument count
MKS-R3102  Invalid call target or unknown method
MKS-R3201  Index out of bounds
MKS-R3301  Invalid numeric operation
MKS-R3401  Property access on non-object
MKS-R3501  Runtime type mismatch
MKS-R3502  Runtime conversion failed

MKS-M4001  Module resolution failed

MKS-I9001  Internal parser state error
```

## Current Groups

### Lexer

```text
MKS-L0001  Invalid lexer token
```

Produced when the lexer cannot turn source text into a valid token.

### Syntax/parser

```text
MKS-S1000  Generic syntax error
MKS-S1001  Unexpected token
MKS-S1002  Expected token missing
MKS-S1003  Invalid assignment target
```

Parser diagnostics are produced before runtime evaluation. They usually know the
exact token column because tokens carry a `start` pointer and `length`.

### Runtime

```text
MKS-R3000  Generic runtime error
MKS-R3001  Undefined name

MKS-R3101  Wrong function/method argument count
MKS-R3102  Invalid call target or unknown method

MKS-R3201  Index out of bounds

MKS-R3301  Invalid numeric operation

MKS-R3401  Property access on non-object

MKS-R3501  Runtime type mismatch
MKS-R3502  Runtime conversion failed
```

Runtime diagnostics are produced while evaluating AST nodes. The runtime usually
knows the current line from `runtime_set_line(node->line)`. When the message
contains a quoted name, the formatter tries to find that name on the current
line and places the caret there.

### Module system

```text
MKS-M4001  Module resolution failed
```

Module diagnostics are separate from generic runtime diagnostics because import
errors tend to involve paths, aliases, std modules, and current working
directory issues.

### Internal

```text
MKS-I9001  Internal parser state error
```

Internal diagnostics mean the interpreter itself reached an unexpected state.
These are not normal user mistakes.

## Implementation

Codes are defined in `Runtime/errors.h` as `MksErrorCode`.
Their user-facing metadata lives in `Runtime/errors.c` in `diagnostic_table`.

The important pieces are:

```text
Runtime/errors.h
  MksErrorCode
  MksDiagnosticInfo
  mks_diagnostic_info(...)

Runtime/errors.c
  diagnostic_table
  runtime_error_code_for(...)
  runtime_print_source_context(...)

Parser/parser_core.c
  parser_error_code_for(...)
  parser_vpanic(...)
```

Runtime currently maps many existing `runtime_error("...")` calls to error codes
by inspecting the message text in `runtime_error_code_for(...)`. This keeps the
old call sites working while centralizing the diagnostic metadata.

Parser diagnostics use `parser_error_code_for(...)` and then read metadata from
the same `diagnostic_table` through `mks_diagnostic_info(...)`.

## Adding A New Error

1. Choose the subsystem and group.

For example, a numeric overflow is runtime + numeric:

```text
Runtime range:       R3000..R3999
Numeric group:       R3300..R3399
Next free code:      R3302
```

2. Add an enum member in `Runtime/errors.h`.

```c
MKS_ERR_RUNTIME_NUMERIC_OVERFLOW = 3302, /* MKS-R3302 */
```

3. Add metadata to `diagnostic_table` in `Runtime/errors.c`.

```c
{
    MKS_ERR_RUNTIME_NUMERIC_OVERFLOW,
    "MKS-R3302",
    "numeric operation",
    "the numeric result is too large to represent",
    "reduce the input range or clamp the value before the operation",
    "if (x < limit) -> Writeln(x * x); <-",
    "check the operands that feed this arithmetic operation"
}
```

4. Route the error to the enum.

For runtime, update `runtime_error_code_for(...)` or, later, call a typed
diagnostic API directly.

For parser, update `parser_error_code_for(...)`.

5. Add or update an `.mks` example in `errorstest/` if the error is user-facing.

## Code Stability Rules

Do:

- Keep codes stable once released.
- Add new codes at the end of the relevant group.
- Use `xx00` for generic fallback and `xx01+` for concrete diagnostics.
- Keep the public code string in sync with the enum number.
- Document every user-facing code here.

Do not:

- Reuse old codes for different meanings.
- Renumber existing diagnostics just to make the list prettier.
- Put a parser error in the runtime range because it is convenient.
- Encode source line numbers or platform-specific details into the error code.

## Why Not Sequential Codes Only?

A purely sequential scheme would look like this:

```text
MKS-E0001  Undefined variable
MKS-E0002  Wrong argument count
MKS-E0003  Unknown method
MKS-E0004  Index out of bounds
```

That is easy at first, but the code gives no hint about where the error came
from. Grouped codes make logs easier to scan:

```text
MKS-S1001  Parser/syntax problem
MKS-R3201  Runtime indexing problem
MKS-M4001  Module resolution problem
```

The grouped layout is slightly more verbose, but it leaves room for growth and
makes diagnostics easier to search and document.
