# Idea Intake And Decision Template

Use this before asking an implementation agent to build a new idea that is still vague.

The purpose is to stop vague ideas from turning into vague code.

## When To Use This

Use this for:

- new product ideas
- workflow changes
- major UI behavior changes
- new compatibility goals
- registry/data-management concepts

Do not send an idea straight to implementation if you cannot fill this out clearly enough.

## Template

```md
# Idea Intake - <short title>

## Problem

- what is wrong today:
- who feels the problem:
- why it matters:

## Desired Behavior

- what should happen:
- what should not happen:

## User Examples

- example 1:
- example 2:
- example 3:

## Non-Examples

- behavior that might sound related but is not the goal:

## Scope

- in scope:
- out of scope:

## Constraints

- technical constraints:
- product constraints:
- data or workflow constraints:

## Edge Cases

- 

## Dependencies

- stage dependencies:
- decision-log dependencies:
- fixture or benchmark dependencies:

## Success Criteria

- 

## Open Questions

- 

## Decision

- approved / rejected / deferred:
- if approved, which stage or slice it belongs to:
```

## Rule

If the idea still has unresolved open questions that affect product policy, do not send it straight to implementation.

Resolve the decision first or add it to:

- `docs/00_control/DECISION_LOG.md`

## Fast Quality Check

Before handing an idea to Claude, ask:

- can I give three concrete examples?
- can I state one clear non-goal?
- do I know which stage this belongs to?
- do I know whether this changes product policy?

If the answer is "no" to any of those, the idea is still intake-stage, not implementation-stage.

## Bottom Line

Well-specified ideas become buildable work.

Vague ideas become expensive cleanup.
