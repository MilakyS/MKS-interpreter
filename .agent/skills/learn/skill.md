# name: learn_from_task
# description: Extract concrete reusable lessons from the completed task

## When to use
Use after review, when the task exposed a repeatable mistake, path ambiguity, validation gap, or decision rule worth preserving.

## Steps

1. Identify only lessons supported by this task.
2. Filter out vague or motivational advice.
3. Write the result into `.agent/lessons.md`.

## Rules

- A lesson must be specific.
- A lesson must be actionable.
- A lesson must be testable.
- A lesson must describe a real repository pattern, not a generic engineering slogan.
- Do not add a lesson if this task did not prove it.

## Output

- Updated `.agent/lessons.md`
- A short note describing what pattern was captured
