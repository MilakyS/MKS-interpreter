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

## 3. Expressions & operators
- Arithmetic: `+ - * / %`
- Compare: `?=`, `!?`, `<`, `>`, `<=`, `>=`
- Logic: `&&`, `||`

## 4. Control flow
```mks
if (x > 0) -> Writeln("pos"); <- else -> Writeln("non"); <-
while (i < 10) -> i =: i + 1; <-
for (var i =: 0; i < 3; i =: i + 1) -> Writeln(i); <-
repeat 5 -> Writeln("tick"); <-
repeat i in 3 -> Writeln(i); <-   // 0,1,2
break; continue;                  // inside loops
```

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

## 8. Watch / on change
```mks
watch x;
on change x -> Writeln("x changed to ", x); <-

x =: 5;   // triggers
```
Register the handler **before** the assignment you want to observe.

## 9. Defer
```mks
defer -> Writeln("cleanup"); <-
Writeln("body");
// prints: body, cleanup
```

## 10. Imports & stdlib
```mks
using "path/to/lib";
using "std/math";   // built-in
Writeln(square(5));
```
Resolution: current file dir → CWD → installed std path. Each module executes once.

## 11. Tests
```mks
test "math" ->
    expect(1 + 1 ?= 2);
<-
```
Run all project tests: `./tests.sh`.

## 12. Errors
Runtime and parser errors show `file:line` plus a hint. Common fixes:
- missing `;`
- missing `<-` block end
- keyword/identifier typo
- using a value of the wrong type

## 13. Tips for speed
- Avoid `var` declarations inside tight loops; declare outside and reuse.
- Use `<-->` for swapping instead of a temp var.
- Keep sanitizer builds off for timing.

## 14. Limits
- Recursion depth guard: 10,000.
- Not production-safe; APIs may change.

