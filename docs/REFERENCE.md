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
using "path/to/file";
using "std/math";
```
Resolution: current dir → CWD → installed std path. Each module runs once.

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

## Errors
Errors report `file:line` plus a hint. Common fixes: add missing `;`, close `<-`, fix keyword typos, check types/undefined vars.

---
For more examples see `examples/` and `tests/cases/`.
