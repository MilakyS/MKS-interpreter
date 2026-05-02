# MKS Review Checklist

## Architecture
- Does this preserve Lexer/Parser/AST/Eval/Runtime/GC separation?
- Is the change VM/compiler-friendly later?

## Low-level language direction
- Is behavior explicit?
- Is memory/reference behavior visible?
- Did this avoid Python/JS-style magic?

## C safety
- Any dangling pointer?
- Any unchecked allocation?
- Any ownership ambiguity?

## GC safety
- Are new GC-managed objects marked?
- Are temporary values rooted?
- Are environments reachable?

## Parser
- Is syntax unambiguous?
- Are errors readable?
- Are AST nodes handled consistently?

## Tests
- Positive case?
- Edge case?
- Invalid case?
- Existing tests still pass?
