# 🐒 MKS Interpreter

**MKS** is a small experimental scripting language and interpreter written in C.

> This is a learning / hobby project — not production-ready.

---

## ✨ Overview

MKS is a simple interpreted language built to explore:

- parsing and AST design  
- runtime systems in C  
- basic garbage collection  
- language syntax experimentation  

The syntax is intentionally a bit unconventional (`->`, `<-`, `=:`), just to try different ideas.

---

## ⚠️ Disclaimer

This project is **not stable**. You will likely encounter:

- bugs  
- inconsistent behavior  
- unfinished features  
- questionable design choices  
- possible memory leaks  

If something behaves oddly — that's part of the process 🙂

---

## 🚀 Features (current state)

- ✅ Numbers  
- ✅ Strings  
- ✅ Arrays  
- ✅ Arithmetic & comparison  
- ✅ Logical operations  
- ✅ `if / else`  
- ✅ `while`, `for`  
- ✅ Functions & recursion  
- ✅ Return values  
- ✅ String methods  
- ✅ Array methods  
- ✅ Indexing (`arr[i]`, `str[i]`)  
- ⚠️ Basic GC (works, but still evolving)

---

Example

```
Writeln("Program start");

var x =: 10;
var arr =: [1, 2, 3];

fnc double(v) ->
    return v * 2;
<-

if(x > 5) ->
    Writeln("x is big");
<-

for(var i =: 0; i < arr.size(); i =: i + 1) ->
    Writeln(arr[i]);
<-

Writeln(double(x));
Writeln("Program end");
```

🧠 Syntax Quick Look
Variables
var x =: 10;
x =: 20;
Functions
fnc add(a, b) ->
    return a + b;
<-
Conditionals
if(x > 0) ->
    Writeln("positive");
<- else ->
    Writeln("not positive");
<-
Loops
while(i < 10) ->
    i =: i + 1;
<-

for(var i =: 0; i < 10; i =: i + 1) ->
    Writeln(i);
<-
🛠️ Build
Requirements
CMake ≥ 3.10
GCC or Clang
Build (Release)
cmake -B build
cmake --build build
Run
./build/mks_run your_file.mks
🧪 Debug (Sanitizers)

Recommended during development:
```
rm -rf build
cmake -B build -DENABLE_ASAN=ON
cmake --build build
```
Run with leak detection:

``` ASAN_OPTIONS=detect_leaks=1 ./build/mks_run your_file.mks ```
🧩 Project Structure
Lexer/        → tokenization
Parser/       → AST + parsing logic
Eval/         → AST evaluation
Runtime/      → values, operators, functions
env/          → variable scope / environment
GC/           → garbage collector
Utils/        → helper utilities
🐛 Things Worth Testing
division / modulo by zero
out-of-bounds access
undefined variables
wrong argument counts
deep recursion
nested arrays / functions
operations on empty structures
🧭 Project Status

Active and evolving.

## 🦍 Senior Review (Recent Updates)

Проект прошел глубокий "Senior Review" и оптимизацию:
- **Ускорение хеш-таблиц**: Переход на побитовые операции (`& mask`) вместо `%`.
- **Оптимизация Лексера**: Удаление лишних аллокаций в `Read_Number`.
- **Исправление GC**: Устранение критического бага при создании массивов.
- **Стабильность**: Итеративная маркировка окружений в GC.

Подробности читайте в [REVIEW.md](./REVIEW.md).

Expect changes in:

syntax
runtime behavior
internal architecture
