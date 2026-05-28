# Stage 01 Slice 01B Closure

## Scope

- active stage: `Stage 01`
- active slice: `01B`
- primary goal: separate persistent context from transient job messaging and make the shipped UI surface honest about unavailable functionality

## What Changed

- kept persistent context in `m_status_line` and transient job feedback in `m_status_job`
- preserved the auto-clear timer on transient job messaging so context and job status no longer overwrite each other
- simplified the File import menu to the formats the current workflow actually supports: `XTF` and `JSF`
- disabled export entries consistently across toolbar, File menu, and Contacts menu
- disabled unfinished contact/annotation actions on the right toolbar and in the Contacts menu instead of leaving them clickable
- disabled unfinished module shortcut buttons so they no longer present as working features

## Files Touched

- `src/ui/MainWindow.h`
- `src/ui/MainWindow.cpp`

## Tests Or Validation

- direct `g++ -fsyntax-only` check for `src/ui/MainWindow.cpp` passed after the status/shipped-surface edits
- searched `MainWindow.cpp` for remaining live placeholder action paths and direct shell-state bypasses
- confirmed unsupported import menu labels were removed from the visible File menu build path

## Gate Status

- gate items completed:
  - status/context responsibilities are separated enough that they no longer overwrite each other
  - the visible UI surface is substantially more honest about unavailable features
- gate items still open:
  - later Stage 01 slices for artifact-store/session, metadata parity, and activation/loading

## Risks / Follow-Ups

- placeholder slot methods still exist in code for disabled actions, but they are no longer expected to be user-reachable from the shipped surface
- full end-to-end app rebuild/link verification is still blocked while the running executable holds the final output path

## What The Next Stage May Assume

- persistent context text and transient job messaging are separate UI channels
- export actions should remain disabled until real export support lands
- the File import surface now matches the currently supported `XTF/JSF` workflow
