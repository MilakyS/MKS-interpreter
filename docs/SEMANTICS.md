# MKS Semantics Contract

This document records current semantic guarantees and explicitly marks unresolved areas.

## Values

Current documented runtime-visible value categories include:
- numbers
- booleans
- strings
- arrays
- functions
- objects/entities
- modules
- pointers
- `null`

Current documented `bool` contract:
- `true` and `false` are surface literals and runtime values
- booleans print as `true` and `false`
- boolean-producing core operators return `bool`
- `Int(true)` returns `1`
- `Int(false)` returns `0`
- `String(true)` returns `true`
- `String(false)` returns `false`
- booleans are truthy/falsy in control-flow as expected
- assignment and parameter passing treat booleans like copied scalar values

Current documented `null` contract:
- `null` is a surface literal and runtime value
- `null` prints as `null`
- `null` is falsy in control-flow and boolean contexts
- `Int(null)` returns `0`
- `String(null)` returns `null`
- `null ?= null` is true
- `null !? null` is false
- assignment and parameter passing treat `null` like a copied scalar value

Current non-guarantees:
- general arithmetic on `null` is not supported
- general comparison ordering with `null` is not supported
- mixed numeric/string comparisons with `null` are not part of the stable contract unless explicitly documented later

`Design in progress`:
- whether MKS should later define broader equality/comparison rules for `null`
- a complete public copy-vs-reference table for every value category

## Assignment and mutation

Current documented behavior:
- `var name =: expr;` binds a variable
- `name =: expr;` updates an existing variable
- `a <--> b;` swaps two assignable locations

Current runtime contract:
- assignment stores a `RuntimeValue`
- for `number`, `bool`, and `null`, assignment copies the value itself
- for `string`, `array`, `object`, `module`, `function`, `blueprint`, and `pointer`, assignment copies the handle to the same underlying runtime object or environment

Practical rule:
- rebinding a variable changes only that variable binding
- mutating a shared array/object/pointed location is visible through every alias that refers to the same underlying storage

Current pointer behavior documented in the repository:
- `&x` takes the address of a variable
- `&arr[i]` takes the address of an array element
- `&obj.field` takes the address of an object field
- `*ptr` reads through a pointer
- `*ptr =: value` writes through a pointer
- taking the address of a temporary expression is rejected

`Design in progress`:
- whether additional pointer targets will be supported
- long-term unsafe/safe boundary for pointer-like operations

## Function arguments

Current runtime contract:
- function arguments are passed by assignment into the callee environment
- this means scalar-like values (`number`, `bool`, `null`) behave as pass-by-value
- handle-like values (`string`, `array`, `object`, `module`, `function`, `blueprint`, `pointer`) give the callee another alias to the same underlying runtime object or environment
- reassigning a parameter inside the callee does not rebind the caller variable
- mutating a shared array/object or writing through a passed pointer is visible outside the callee

Examples under the current contract:
- `fnc f(x) -> x =: 2; <-` does not change the caller's numeric variable
- `fnc f(xs) -> xs[0] =: 2; <-` changes the caller-visible array contents
- `fnc f(p) -> *p =: 2; <-` changes the pointed target

`Design in progress`:
- whether future surface syntax will distinguish explicit borrow/reference passing from ordinary handle copying
- whether strings remain effectively immutable at the language surface

## Functions and control flow

Current documented behavior:
- functions use `fnc name(args) -> ... <-`
- `return` exits a function
- `break` and `continue` propagate through loop control
- `switch (value) -> case ... default ... <-` selects one body without fallthrough
- `else if` is supported as a chained conditional surface form

Semantic intent:
- control flow should stay explicit and runtime-transparent

Current `if / else if / else` contract:
- conditions are evaluated in source order
- the first truthy branch body runs
- if no condition matched, the optional final `else` body runs
- `else if` currently desugars to an `if` node placed in the `else` branch; it does not have a separate runtime model

Current `switch` contract:
- the switch value is evaluated once
- cases are checked in source order
- the first case whose value equals the switch value runs
- `default` runs only when no case matched
- when no case matches and there is no `default`, the switch statement produces no useful value and simply completes
- matching uses the current runtime equality contract

Current boolean operator contract:
- `?=`, `!?`, `<`, `>`, `<=`, and `>=` produce `bool`
- `&&` and `||` produce `bool`
- numeric values remain valid condition inputs through truthiness rules for compatibility

Current non-guarantees:
- there is no fallthrough between cases
- pattern matching, ranges, or destructuring are not part of the switch contract

## Objects, entities, and methods

Current documented behavior:
- `entity` creates object-like constructors
- methods are invoked through the object
- object fields can be addressed with pointers

Current `entity/object` contract:
- an `entity` declaration creates a blueprint value bound by name
- calling that blueprint constructs a fresh object environment
- constructor arguments are bound into the object initialization environment
- `init` runs once during construction if it exists
- methods are installed onto the constructed object and execute with `self` bound to that object
- object fields are dynamic: fields come into existence when assigned
- reading a missing object field returns `null`
- assigning an object field replaces or creates that field on the target object
- object instances share the normal RuntimeValue handle/aliasing model; rebinding an object variable does not clone the object

Current stable scope:
- `entity` is the primary high-level object-constructor mechanism in MKS
- `self.field =: value` inside `init` or methods is in contract
- methods may read/write object fields and return `null`
- entity instances may store `null` in fields

Current non-guarantees:
- there is no declared-field or schema contract yet
- there is no inheritance contract
- there is no visibility/private-field contract
- missing field access returning `null` is current behavior for objects, not a promise of a future static object model

`Design in progress`:
- the final object/value model for entities
- whether objects stay purely dynamic or gain stricter low-level semantics later

## Modules and imports

Current documented behavior:
- `using` binds a namespace object
- imports do not copy exports into the current scope
- module resolution supports relative paths, stdlib modules, and package imports via the nearest `mks.toml`
- imported modules load declarations once
- top-level executable statements are skipped during import
- exports are accessed through the module namespace

Current declaration forms used during import:
- `using`
- `var`
- `fnc`
- `entity`
- `extend`
- `export`

Current module lifetime contract:
- file/native modules are cached per interpreter context
- repeated import of the same resolved module returns the same exports namespace object
- module-private state captured by exported functions remains alive for the full interpreter context lifetime
- loaded module environments are retained even if the importing scope later drops its alias

Current import-time execution boundary:
- declaration loading during import may still raise ordinary runtime errors
- such errors remain fatal to the current execution run unless the surrounding runner boundary catches them
- there is no recoverable module-import exception surface yet

`Design in progress`:
- cycle semantics beyond the currently implemented loading-namespace behavior
- whether module initialization hooks will exist
- whether `using` without `as` is a stable contract everywhere

## Watch and defer

Current documented surface:
- `defer -> ... <-`
- `watch x;`
- `on change x -> ... <-`

Current `watch` contract:
- `watch name;` registers interest in a variable name
- `on change name -> ... <-` registers a handler for that name
- handlers run synchronously after a matching variable binding is updated
- the observed value passed to callable handlers is the post-update value
- handlers are stored per interpreter context until cleared or context disposal
- handler closures and callable handlers are retained for the current interpreter context until `watch.clear()` or context disposal

Current stable trigger boundary:
- direct variable assignment/update of the watched name is in contract
- updates that happen through ordinary environment binding paths are in contract

Current non-guarantees:
- pointer-mediated writes are not part of the stable `watch` contract
- deep/reactive watching of object fields or array elements is not part of the stable contract
- ordering across multiple handlers with the same name is not part of the public contract
- runtime errors raised by watch handlers are not isolated; they abort the current execution run through the normal runtime error boundary

`Design in progress`:
- whether `watch` remains a core language feature or becomes a narrower mechanism
- whether pointer/object/array mutation should become observable through `watch`

## Extend

Current documented surface:
- `extend array -> ... <-`
- `extend string -> ... <-`
- `extend number -> ... <-`

Current `extend` contract:
- an `extend` declaration registers methods for one built-in target family: `array`, `string`, or `number`
- registered methods become available through normal method-call syntax on that target family
- method bodies execute with `self` bound to the call target
- extension methods close over the environment that existed when the `extend` declaration ran
- registered extensions live for the current interpreter context

Current stable scope:
- extensions are context-global after registration; they are not namespace-scoped
- array/string/number method dispatch may use registered extensions when no built-in method handled the call
- registering a duplicate method name for the same target family is a runtime error
- when an imported module is loaded once from the module cache, its `extend` declarations are applied once as part of that first load
- extension method closures remain alive for the current interpreter context lifetime

Current non-guarantees:
- there is no override or shadowing policy for duplicate extension names; duplicates fail instead of replacing older registrations
- module-import semantics for `extend` beyond current load-once import behavior are not yet frozen
- extension visibility ordering relative to future module/package features is not frozen
- runtime errors raised during extension method execution are not isolated from the current run

## Builtins

Current documented builtins include:
- `Write`
- `Writeln`
- `Read`
- `ReadLine`
- `ReadWord`
- `Int`
- `String`
- `expect`
- `Object`

Current documented input rule:
- `Read()` is an alias of `ReadLine()`
- `ReadWord()` returns the first whitespace-separated word from a read line

## Errors

Current documented behavior:
- syntax and runtime errors report file/line context with hints
- runtime execution helpers are expected to return status codes at controlled boundaries instead of unconditionally terminating the process

## Semantic posture

Until a stronger public spec exists, MKS should prefer:
- explicit behavior over convenience magic
- visible mutation/reference behavior
- semantics that could survive a later VM/compiler implementation
