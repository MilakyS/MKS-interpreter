# MKS Language Reference (Quick)

## Comments
```mks
// single line
```

## Values
- number (double)
- bool (`true`, `false`)
- string `"text"`
- array `[1, 2, 3]`
- null

`bool` rule:
- `true` and `false` are real literals/values
- booleans print as `true` and `false`
- comparisons and logical operators produce booleans
- `Int(true)` returns `1`
- `Int(false)` returns `0`
- `String(true)` prints as `true`

`null` rule:
- `null` is a real literal/value
- `null` is falsy
- `null ?= null` is true
- `Int(null)` returns `0`
- `String(null)` prints as `null`
- arithmetic and ordered comparisons with `null` are not part of the stable contract

## Variables
```mks
var x =: 10;
x =: 20;
a <--> b;          // swap
```

Assignment rule:
- numbers, booleans, and `null` copy by value
- strings, arrays, objects, modules, functions, and pointers copy a handle to shared runtime storage
- rebinding a parameter/local name does not change the caller binding; mutating shared arrays/objects or writing through a pointer does

## Pointers
```mks
var x =: 10;
var p =: &x;       // address of variable
*p =: 20;          // write through pointer
Writeln(x);        // 20

fnc inc(ptr) ->
    *ptr =: *ptr + 1;
<-
inc(&x);

var xs =: [1, 2, 3];
var item =: &xs[1];
*item =: 9;
```

Pointers can target variables, array elements, and object fields. Taking the
address of a temporary expression is rejected.

## Functions
```mks
fnc add(a, b) ->
    return a + b;
<-
add(1, 2);
```

## Control Flow
```mks
if (c) -> ... <- else -> ... <-
if (a) -> ... <- else if (b) -> ... <- else -> ... <-

while (cond) -> ... <-
for (var i =: 0; i < 10; i =: i + 1) -> ... <-

repeat 5 -> ... <-
repeat i in 5 -> ... <-   // i = 0..4

switch (value) ->
    case 1 -> ... <-
    case 2 -> ... <-
    default -> ... <-
<-

break; continue;
```

`if` rule:
- `else if` is supported
- conditions are checked from left to right
- the first truthy branch runs
- `else` is optional

`switch` rule:
- first matching `case` body runs
- `default` is optional
- no fallthrough; each `case` owns its own body block
- matching uses the current runtime equality contract

## Defer / Watch
```mks
defer -> cleanup(); <-

watch x;
on change x -> Writeln("x changed"); <-
```

`watch` rule:
- stable contract covers direct updates of the watched variable name
- handler runs synchronously after the update
- deep watching and pointer-mediated propagation are not part of the stable contract

## Entities & Methods
```mks
entity User(name) ->
    init -> self.name =: name; <-
    method hi() -> Writeln("Hi ", self.name); <-
<-

var u =: User("Ann");
u.hi();
```

## Extend built-ins
```mks
extend array ->
    method sum() -> var s =: 0; var i =: 0;
        while (i < self.len()) -> s =: s + self[i]; i =: i + 1; <-
        return s;
    <-
<-
[1,2,3].sum(); // 6
```

`extend` rule:
- stable targets are `array`, `string`, and `number`
- registered methods become context-global for that target family
- duplicate method names for the same target family are rejected at registration time
- importing the same module again does not re-register its `extend` declarations

## Imports & Stdlib
```mks
using "./path/to/file";
using std.math as math;
using std.json as json;
using std.path as path;
using std.process as process;
using cool.lib.http.client as client;
```
`using` binds a module namespace object. Exports are accessed through the alias or default module name.

Resolution:
- `./...` and `../...` resolve relative to the current file
- `std.*` resolves from stdlib
- other dotted ids resolve as package imports from the nearest `mks.toml`

Imported modules load declarations once. Top-level executable statements are skipped during import.

### `std.json`
```mks
using std.json as json;

var v =: json.parse("{\"name\":\"mks\",\"n\":2,\"ok\":true,\"none\":null}");
Writeln(v.name);
Writeln(json.stringify([1, "x", null]));
```

Current baseline:
- `json.parse(text)` supports objects, arrays, strings, numbers, `true`, `false`, and `null`
- `json.stringify(value)` supports objects, arrays, strings, numbers, booleans, and `null`
- parsed JSON booleans map to core MKS booleans
- `json.stringify(true)` emits `true`
- `\uXXXX` escapes are not supported yet

### `std.process`
```mks
using std.process as process;

Writeln(process.args());
Writeln(process.cwd());
```

Current baseline:
- `process.args()` returns the current CLI argument vector as an array of strings
- `process.cwd()` returns the current working directory
- `process.exit([code])` aborts the current run with the given exit status; default is `0`

### `std.tty`

```mks
using std.tty as tty;

tty.clear();
tty.window(2, 2, 40, 8, "Title", 2);
tty.text_wrap(3, 3, 38, 6, "Русский and English text wrap safely.", 1);
tty.flush();
```

Useful calls:

- `tty.raw()`, `tty.cooked()`, `tty.restore()` manage terminal mode
- `tty.alt_screen()`, `tty.normal_screen()` switch terminal buffers
- `tty.read_key()` waits for a key; `tty.read_key_timeout(ms)` returns `null`
  on timeout
- `tty.size()` returns `{ w, h }`; `tty.size_or(w, h)` uses fallback dimensions
  outside real terminals
- `tty.text(...)` clips by terminal columns, not bytes
- `tty.text_wrap(x, y, w, h, text, clear)` wraps UTF-8 text inside a rectangle
- `tty.window`, `tty.floating_window`, `tty.button`, `tty.input_box` provide
  basic TUI primitives

### `std.path`
```mks
using std.path as path;

Writeln(path.join("dir", "sub", "file.txt"));
Writeln(path.basename("dir/sub/file.txt"));
Writeln(path.dirname("dir/sub/file.txt"));
```

Current baseline:
- `path.join(...)` joins path parts with `/`
- `path.basename(path)` returns the final name component
- `path.dirname(path)` returns the parent path or `.`
- `path.extname(path)` returns the final extension including `.`
- `path.stem(path)` returns the basename without the final extension
- `path.normalize(path)` collapses repeated `/` and `\\` separators to `/`

## Tests
```mks
test "math add" ->
    expect(1 + 1 ?= 2);
<-
```

## Built-in Output
```mks
Write(x, y);
Writeln(x, y);   // newline
```

## Built-in Input
```mks
var line =: Read();                  // alias of ReadLine()
var name =: ReadLine("Name: ");      // print prompt, then read full line
var word =: ReadWord("Word: ");      // print prompt, then read first word
```

`Read`, `ReadLine`, and `ReadWord` return strings. The trailing `\n` or `\r\n`
is removed. If standard input reaches EOF, they return `""`.

When the first argument is a string, it is printed to stdout and flushed before reading:

```mks
var age =: ReadLine("Age: ");
```

`ReadWord()` returns the first whitespace-separated word from the input line:

```mks
// input: hello world
Writeln(ReadWord()); // hello
```

The input buffer is limited to 8191 characters per call.

## Built-in Conversions
```mks
var n =: Int("42");
var text =: String([1, "x"]);
```

`Int(value)` accepts numbers, booleans, strings, and `null`. Strings are parsed as numeric
text and must not contain trailing non-whitespace characters.

`String(value)` returns the same text that MKS would print for the value.

## Errors
Errors report `file:line` plus a hint. Common fixes: add missing `;`, close `<-`, fix keyword typos, check types/undefined vars.

---
For more examples see `examples/` and `tests/cases/`.
