# Bug Investigation Template

Use this template for deep bugs that require real engineering, not just a quick patch.

This is especially useful for:

- cross-module bugs
- intermittent failures
- cache versus raw inconsistencies
- parser correctness bugs
- loading/state bugs that are easy to patch wrongly

## Template

```md
# Bug Investigation - <short title>

## Summary

- bug id:
- reported by:
- area:
- severity:

## User-Visible Symptom

- 

## Expected Behavior

- 

## Reproduction

1. 
2. 
3. 

## Current Evidence

- logs:
- screenshots:
- sample file / fixture:
- affected files/modules:

## Suspected Invariant Being Broken

- 

## Root Cause Hypotheses

- hypothesis 1:
- hypothesis 2:
- hypothesis 3:

## Investigation Plan

- reproduce
- isolate
- instrument if needed
- confirm root cause
- add failing test or repeatable repro
- patch
- verify regression coverage

## Confirmed Root Cause

- 

## Fix Scope

- files likely to change:
- non-goals:

## Validation

- failing repro before:
- passing repro after:
- tests added or run:

## Regression Risks

- 

## Closure

- what changed:
- what remains:
- follow-up work:
```

## Rules For Use

- do not start with a patch if reproduction is still fuzzy
- do not merge root-cause hypotheses into "the cause" too early
- do not treat symptom removal as proof of correctness
- do not close the bug without a repeatable before/after story

## Recommended Sequence

1. Reproduce the bug.
2. Identify the broken invariant.
3. Add a failing test or stable repro.
4. Confirm the actual root cause.
5. Patch the narrowest correct layer.
6. Validate the fix and the regression story.

## Bottom Line

Deep bugs should be investigated like incidents, not guessed at like UI polish tasks.
