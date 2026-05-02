# MKS Language Direction

MKS is a low-level-oriented programming language with an interpreter-first implementation.

The interpreter is the first implementation strategy, not the final identity of the language.

## MKS is trying to be

- explicit
- memory-aware
- runtime-transparent
- systems-friendly
- embeddable
- eventually VM/compiler-ready

## MKS is not trying to be

- a Python replacement
- a JavaScript clone
- a browser scripting language
- a purely high-level dynamic language
- a toy educational language

## Core philosophy

MKS should feel like a systems language that currently runs through an interpreter.

Language and runtime design should prefer:
- explicit behavior over magic
- predictable runtime semantics
- visible memory and reference behavior
- small primitives that can later map to bytecode/native code
