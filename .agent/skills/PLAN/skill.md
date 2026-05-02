# name: plan_task
# description: Analyze the task and create a minimal safe implementation plan

## When to use
Use before implementing any non-trivial change.

## Steps

1. Read the relevant code and local contracts.
2. Identify:
   - affected files
   - dependencies
   - risks
3. Write the plan into `.agent/PLANS.md`:
   - objective
   - non-goals
   - files to modify
   - step-by-step implementation
   - test plan

## Rules

- Do not write code yet.
- Prefer the smallest possible implementation.
- Avoid touching unrelated subsystems.
- Highlight GC and memory risks explicitly when applicable.

## Output

- Updated `.agent/PLANS.md`
- A short summary of the plan
