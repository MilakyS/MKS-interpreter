# MKS Language Reference (Quick)

## Comments
```mks
// single line
```

## Values
- number (double)
- string `"text"`
- array `[1, 2, 3]`
- null

## Variables
```mks
var x =: 10;
x =: 20;
a <--> b;          // swap
```

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

while (cond) -> ... <-
for (var i =: 0; i < 10; i =: i + 1) -> ... <-

repeat 5 -> ... <-
repeat i in 5 -> ... <-   // i = 0..4

break; continue;
```

## Defer / Watch
```mks
defer -> cleanup(); <-

watch x;
on change x -> Writeln("x changed"); <-
```

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

## Imports & Stdlib
```mks
using "./path/to/file";
using std.math as math;
using cool.lib.http.client as client;
```
`using` binds a module namespace object. Exports are accessed through the alias or default module name.

Resolution:
- `./...` and `../...` resolve relative to the current file
- `std.*` resolves from stdlib
- other dotted ids resolve as package imports from the nearest `mks.toml`

Imported modules load declarations once. Top-level executable statements are skipped during import.

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
var line =: Read();             // read full line
var name =: Read("Name: ");     // print prompt, then read full line
var word =: Read(0);            // read first word only
```

`Read` returns a string. The trailing `\n` or `\r\n` is removed. If standard input reaches EOF, it returns `""`.

When the first argument is a string, it is printed to stdout and flushed before reading:

```mks
var age =: Read("Age: ");
```

When the first argument is integer `0`, only the first whitespace-separated word is returned:

```mks
// input: hello world
Writeln(Read(0)); // hello
```

The input buffer is limited to 8191 characters per call.

## Built-in Conversions
```mks
var n =: Int("42");
var text =: String([1, "x"]);
```

`Int(value)` accepts numbers, strings, and `null`. Strings are parsed as numeric
text and must not contain trailing non-whitespace characters.

`String(value)` returns the same text that MKS would print for the value.

## Errors
Errors report `file:line` plus a hint. Common fixes: add missing `;`, close `<-`, fix keyword typos, check types/undefined vars.

---
For more examples see `examples/` and `tests/cases/`.
