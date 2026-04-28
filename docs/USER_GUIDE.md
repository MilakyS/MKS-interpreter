# MKS User Guide

A concise, example-driven guide to MKS.

## 1. Running code
```
./build/mks file.mks
./build/mks --repl      # interactive
```

## 2. Values & variables
```mks
var n =: 42;
var s =: "hi";
var arr =: [1, 2, 3];
arr[0];          // indexing
arr[1] =: 5;     // set
```
Swap without temp:
```mks
var a =: 1; var b =: 2;
a <--> b;
```

Assignment model:
- numbers, booleans, and `null` behave like plain copied values
- strings, arrays, objects, modules, functions, and pointers behave like copied handles to shared runtime state
- reassigning a function parameter does not rebind the caller variable; mutating a shared array/object or writing through a pointer does affect the shared target

`bool` baseline:
- `true` and `false` are real values
- they print as `true` and `false`
- comparisons and logical operators produce booleans
- `Int(true)` gives `1`
- `Int(false)` gives `0`
- `String(true)` gives `true`

`null` baseline:
- `null` is a real value
- it is falsy in logic
- `null ?= null` is true
- `Int(null)` gives `0`
- `String(null)` gives `null`
- do not assume broad arithmetic or ordered comparison rules for `null`

## 3. Expressions & operators
- Arithmetic: `+ - * / %`
- Compare: `?=`, `!?`, `<`, `>`, `<=`, `>=`
- Logic: `&&`, `||`

## 4. Control flow
```mks
if (x > 0) -> Writeln("pos"); <- else -> Writeln("non"); <-
if (x ?= 0) -> Writeln("zero"); <- else if (x > 0) -> Writeln("pos"); <- else -> Writeln("neg"); <-
while (i < 10) -> i =: i + 1; <-
for (var i =: 0; i < 3; i =: i + 1) -> Writeln(i); <-
repeat 5 -> Writeln("tick"); <-
repeat i in 3 -> Writeln(i); <-   // 0,1,2
switch (kind) ->
    case "a" -> Writeln("A"); <-
    case "b" -> Writeln("B"); <-
    default -> Writeln("other"); <-
<-
break; continue;                  // inside loops
```

Current baseline:
- `else if` is supported as a chained conditional form
- conditions in an `if / else if / else` chain are checked from left to right
- the first truthy branch runs
- `switch` evaluates its value once
- the first matching `case` body runs
- `default` is optional
- there is no fallthrough between cases

## 5. Functions
```mks
fnc add(a, b) ->
    return a + b;
<-
Writeln(add(2, 3));
```

## 6. Entities (lightweight objects)
```mks
entity User(name) ->
    init -> self.name =: name; <-
    method hi() -> Writeln("Hi ", self.name); <-
<-

var u =: User("Ann");
u.hi();
```

## 7. Extend built-ins
```mks
extend array ->
    method last() ->
        if (self.len() ?= 0) -> return null; <-
        return self[self.len() - 1];
    <-
<-
[1,2,3].last();
```

Current baseline:
- `extend` works for `array`, `string`, and `number`
- registered methods become available globally in the current interpreter context
- duplicate method names for the same target family fail at registration
- importing the same module again does not apply its `extend` declarations twice

## 8. Watch / on change
```mks
watch x;
on change x -> Writeln("x changed to ", x); <-

x =: 5;   // triggers
```
Register the handler **before** the assignment you want to observe.

Current baseline:
- the stable contract covers direct updates of the watched variable name
- handler execution is synchronous after the update
- do not assume deep watching of arrays/objects or propagation through pointers

## 9. Defer
```mks
defer -> Writeln("cleanup"); <-
Writeln("body");
// prints: body, cleanup
```

## 10. Imports & stdlib
```mks
using "./path/to/lib" as lib;
using std.math as math;
using std.json as json;
using std.path as path;
using std.process as process;
Writeln(math.square(5));
```
`using` now works as a namespace import: it binds one module object and does not spill exported names into the local scope.

Resolution:
- `./...` and `../...` are relative imports
- `std.*` loads stdlib modules
- other dotted ids are package imports resolved from the nearest `mks.toml`

Imported modules load declarations once. Top-level executable statements are skipped during import.

### JSON

```mks
using std.json as json;

var cfg =: json.parse("{\"name\":\"mks\",\"enabled\":true,\"items\":[1,2]}");
Writeln(cfg.name);
Writeln(cfg.enabled);              // true
Writeln(json.stringify(cfg.items)); // [1,2]
```

Current baseline:
- `json.parse(text)` supports JSON objects, arrays, strings, numbers, booleans, and `null`
- parsed booleans become core MKS booleans
- `json.stringify(value)` supports objects, arrays, strings, numbers, booleans, and `null`
- `json.stringify(true)` produces `true`
- `\uXXXX` escapes are not supported yet

### Process

```mks
using std.process as process;

var args =: process.args();
Writeln(args[0]);
Writeln(process.cwd());
```

Current baseline:
- `process.args()` returns CLI arguments as an array of strings
- `process.cwd()` returns the current working directory
- `process.exit([code])` stops the current run with the given exit status

### Path

```mks
using std.path as path;

Writeln(path.join("dir", "sub", "file.txt"));
Writeln(path.basename("dir/sub/file.txt"));
Writeln(path.dirname("dir/sub/file.txt"));
```

Current baseline:
- `path.join(...)` joins path parts using `/`
- `path.basename(path)` returns the last path component
- `path.dirname(path)` returns the parent path or `.`
- `path.extname(path)` returns the final extension including `.`
- `path.stem(path)` returns the basename without the final extension
- `path.normalize(path)` collapses repeated `/` and `\\` separators to `/`

### Installing stdlib

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build
```

Local user install:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
cmake --install build
```

This installs `mks` into `bin/` and stdlib into `share/mks/std/`. The runtime resolves installed stdlib automatically.

## 11. Input
`Read`, `ReadLine`, and `ReadWord` are built-in functions for reading from standard input. They always return strings.

```mks
var name =: ReadLine("Name: ");
Writeln("Hello, ", name);
```

If the first argument is a string, MKS prints it before waiting for input. This is useful for prompts:

```mks
var answer =: ReadLine("Type something: ");
```

`Read()` is kept as an alias of `ReadLine()`. `ReadLine()` reads one full line and removes the trailing newline:

```mks
var line =: ReadLine();
Writeln(line);
```

`ReadWord()` reads one line and returns only the first whitespace-separated word:

```mks
var word =: ReadWord();
Writeln(word);
```

Examples:
- input `hello world` with `ReadLine()` returns `"hello world"`
- input `hello world` with `ReadWord()` returns `"hello"`
- end-of-file returns an empty string `""`

The input buffer is limited to 8191 characters per call.

## 12. Conversions
Use `Int(value)` for numeric conversion and `String(value)` for printable text conversion.

```mks
var age_text =: "42";
var age =: Int(age_text);

Writeln(String(age + 1));       // 43
Writeln(String([1, "x"]));      // [1, x]
```

`Int` accepts numbers, booleans, strings, and `null`. Invalid strings fail with a conversion diagnostic.

`String` accepts any runtime value.

## 13. Tests
```mks
test "math" ->
    expect(1 + 1 ?= 2);
<-
```
Run all project tests: `./tests.sh`.

## 13. Errors
Runtime and parser errors show `file:line` plus a hint. Common fixes:
- missing `;`
- missing `<-` block end
- keyword/identifier typo
- using a value of the wrong type

## 14. Tips for speed
- Avoid `var` declarations inside tight loops; declare outside and reuse.
- Use `<-->` for swapping instead of a temp var.
- Keep sanitizer builds off for timing.

## 15. Limits
- Recursion depth guard: 10,000.
- Not production-safe; APIs may change.
