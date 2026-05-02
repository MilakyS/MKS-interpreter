# name: ruthless_audit
# description: Ruthlessly audit MKS code for GC safety, memory issues, performance regressions, and architecture damage

## When to use
Use after changes touching:
- `GC/`
- `Runtime/`
- `Eval/`
- `env/`
- `Parser/`
- arrays / strings / objects
- pointers / references
- modules
- loops / recursion
- allocation-heavy code

Also use before release-oriented review.

## Mindset

Assume the implementation is wrong until proven otherwise.

Your job is to find:
- GC bugs
- hidden allocations
- leaks
- use-after-free risks
- accidental quadratic behavior
- slow hot paths
- architecture hacks
- unstable language semantics

## Audit focus

1. GC correctness
   - new GC-managed kinds are fully integrated
   - temporary values stay rooted
   - recursive reachability stays safe

2. Memory safety
   - ownership is explicit
   - allocations are checked where required
   - no stale pointers or invalid frees

3. Performance
   - hot paths do not pick up avoidable allocation or lookup churn
   - repeated module or string work is justified

4. Architecture
   - no layer-smearing shortcuts
   - no hacks that block later VM/compiler direction

5. Language semantics
   - the implementation does not silently widen or blur semantics

## Output

- blocking findings
- non-blocking risks
- the first places that should be reworked before trusting the change
