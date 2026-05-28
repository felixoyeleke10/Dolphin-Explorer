# Hard Problem Execution Protocol

Use this protocol when the coding problem is difficult enough that normal "implement the feature" prompting tends to produce bad judgment.

Examples:

- architecture-sensitive refactors
- cross-module correctness bugs
- parser changes with hidden invariants
- concurrency or loading changes
- anything likely to trigger model overconfidence

## Goal

Reduce bad model judgment by forcing a safer execution pattern.

## Protocol

### Step 1: Define The Slice Before Coding

Before implementation, the agent should write a short preflight note that states:

- active stage
- active slice
- primary goal
- files likely to change
- invariants that must remain true
- tests or validation expected at the end

If the agent cannot state that clearly, the slice is still too large.

### Step 2: Name The Main Risk

The agent should explicitly state the biggest risk, for example:

- state drift
- cache/index mismatch
- wrong packet interpretation
- stale UI completion path
- memory blowup

This prevents shallow "looks good" implementation on a deep problem.

### Step 3: List Failure Modes

Before editing code, list the most likely ways the slice could go wrong.

Examples:

- works for raw path but not cache path
- fixes one modality and breaks another
- uses the wrong coordinate semantics
- passes compile but violates stage policy
- adds hidden stage bleed

### Step 4: Implement The Smallest Safe Slice

Do not attack the whole problem at once.

Choose the smallest slice that:

- changes one core behavior
- can be validated
- does not require silently adopting next-stage work

### Step 5: Self-Review Against The Rules

Before declaring success, the agent should review the result against:

- `docs/00_control/DECISION_LOG.md`
- `docs/00_control/STAGE_GATE_CHECKLIST.md`
- the active stage doc

The review should answer:

- did I cross stage scope?
- did I invent product policy?
- did I leave the system in a more testable state?
- did I satisfy the promised validation?

### Step 6: Produce A Closure Note

Every hard-problem slice should end with a closure note in:

- `docs/90_closure_notes/`

That note should say:

- what was changed
- what was validated
- what risk remains
- what the next slice should assume

## Prompt Pattern For Hard Problems

If you are using Claude interactively, prefer prompts like:

- "Work only on Stage 01 Slice 01C."
- "Before coding, explain the invariants, failure modes, and tests."
- "Do not solve adjacent issues unless they block this slice."
- "If you hit a product decision not locked in the decision log, stop."

Avoid prompts like:

- "Fix all the MainWindow issues."
- "Make the import workflow robust."
- "Clean this architecture up."

Those are too broad and trigger bad judgment.

## Escalation Rule

If the agent seems uncertain on a hard problem, ask for:

- root-cause note first
- implementation second

This is especially important for:

- parser logic
- cache contracts
- concurrency
- UI state machines

## Acceptance Rule

Do not accept a hard-problem change only because:

- it compiles
- the patch is large
- the explanation sounds smart

Accept it only if:

- the slice stayed within scope
- the claimed invariant is actually protected
- the validation matches the risk

## Bottom Line

Hard problems should be run as disciplined engineering slices, not as one-shot coding challenges.
