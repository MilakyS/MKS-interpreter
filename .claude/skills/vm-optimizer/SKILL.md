---
name: mks-vm-bytecode-optimizer
description: Optimize MKS VM bytecode safely by detecting opcode patterns, preserving stack semantics, and requiring full opcode integration.
---

# MKS VM Bytecode Optimizer

## When to Use

Use this skill when modifying:

- `VM/`
- `Runtime/vm_compile.*`
- `Runtime/vm_exec.*`
- `Runtime/vm_peephole.*`
- bytecode dump/disassembler code
- opcode enum/encoding
- compiler emission
- VM performance paths

Use this skill when adding, changing, or reviewing bytecode optimizations.

## Goal

Improve VM performance by reducing:

- bytecode size
- instruction dispatch count
- stack churn
- temporary values
- unnecessary constants
- avoidable allocations

Correctness is more important than speed.

## Core Rule

Every bytecode optimization must preserve observable MKS behavior.

Tree-walk semantics and VM semantics must stay equivalent.

## Required Checks

### 1. Opcode Completeness

Every new opcode must include:

- opcode enum entry
- compiler emission or peephole rewrite
- execution handler
- disassembler/dump support
- opcode length definition
- stack effect documentation
- tests

Never add an opcode only to the executor.

### 2. Stack Effect Safety

For every optimization, write the stack effect before and after.

Example:

```text
Before:
GET_LOCAL x      ; push local
CONSTANT c       ; push const
ADD              ; pop 2, push result
SET_LOCAL x      ; pop value, assign local, push/keep assigned value depending on VM convention
POP              ; discard result

After:
ADD_LOCAL_CONST x, c

Verify:

no stack underflow
no leftover values
no missing result
no changed assignment behavior
no changed error behavior
3. Type Safety

Do not assume types unless proven.

Allowed:

OP_STRING_APPEND_LOCAL_CONST

only if the constant is known to be string at compile/peephole time.

If runtime value type can vary, keep runtime checks.

4. Bytecode Encoding Safety

For every opcode, verify:

instruction size
operand order
operand width
endian handling for u16
jump offset compatibility
disassembler decoding
ip advancement

Bad:

case OP_MY_OPCODE:
    slot = READ_BYTE();
    constant = READ_SHORT();
    ...

but get_opcode_len() still returns 1.

5. Peephole Safety

A peephole rewrite is allowed only when:

the matched instruction sequence is exact
no instruction in the sequence can throw in a different order
stack behavior is identical
source line/debug info remains acceptable
constants are type-checked when needed
jump targets do not land inside the replaced sequence

Never rewrite across labels, jumps, loop boundaries, or exception/error-sensitive boundaries unless explicitly proven safe.

6. Dispatch Reduction

Prefer replacing common instruction chains with fused opcodes.

Good candidates:

GET_LOCAL x
CONSTANT c
ADD
SET_LOCAL x
POP

to:

ADD_LOCAL_CONST x, c
GET_LOCAL x
CONSTANT c
SUB
SET_LOCAL x
POP

to:

SUB_LOCAL_CONST x, c
GET_LOCAL x
CONSTANT c
MUL
SET_LOCAL x
POP

to:

MUL_LOCAL_CONST x, c
GET_LOCAL x
CONSTANT c
ADD
SET_LOCAL x
POP

when c is string:

STRING_APPEND_LOCAL_CONST x, c
7. Allocation Awareness

Do not optimize instruction count while increasing allocation pressure.

For string operations, check:

temporary strings
repeated concat in loops
GC pressure
StringBuilder opportunity

If the pattern appears inside a loop and performs repeated string concat, prefer a builder strategy over endlessly fusing concat opcodes.

8. Constants and Locals

Check for:

constant index overflow
local slot overflow
invalid slot access
stale local value after mutation
constant pool dedup assumptions
9. Debug Dump Consistency

Any bytecode optimization must be visible and understandable in --dump-bytecode.

The dump should show fused opcodes clearly:

OP_ADD_LOCAL_CONST slot=0 const=1
OP_STRING_APPEND_LOCAL_CONST slot=2 const=5
10. Testing Requirements

Every optimization needs tests for:

normal execution
bytecode dump output
wrong type behavior
boundary operands
interaction with locals
interaction with loops if pattern is loop-relevant
regression case for the original pattern
Red Flags

Immediately stop and inspect if you see:

new opcode without get_opcode_len
executor handler without disassembler support
peephole rewrite without stack effect proof
optimization that changes error timing
optimization across jump targets
string concat optimization inside loop without GC/allocation discussion
opcode that assumes local type without validation
bytecode dump lying about operands
missing tests
Output Format

When reviewing or proposing a VM optimization, respond in this format:

[BYTECODE OPTIMIZATION]

Pattern:
Before:
After:

Why safe:
Stack effect:
Type assumptions:
Bytecode size change:
Dispatch reduction:
Allocation impact:

Files required:
- enum:
- compiler/peephole:
- executor:
- disassembler:
- opcode length:
- tests:

Risks:
Required tests:
Final Rule

Do not trade correctness for speed.

A bytecode optimization is valid only if it is:

semantically identical
stack-safe
type-safe
dump-visible
tested
measurable
