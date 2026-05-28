# Dolphin Explorer Project Storage Model

## Intent

Dolphin Explorer should use a project-first storage model similar in spirit to professional survey software such as Moga SeaView.

The goal is to keep projects:

- self-contained
- easy to understand
- portable
- fast to reopen
- clearly separated from internal implementation details
- clearly separated from temporary/internal cache behavior

This should feel like a deliberate survey-project workflow, not like a generic file-open flow.

Important clarification:

- managed Dolphin Explorer project data should live inside the project folder
- raw survey source files may remain outside the project folder by default

That means a normal project is self-contained for Dolphin-managed data and project state, but not necessarily for original raw survey sources unless a future archive mode is added.

## Current Implementation Status (2026-04-10)

### Implemented Now

- `.dlp` is the current project extension in new/save flows
- `.dlp` plus legacy `.pelagic` are accepted in open flows
- `.dlpd` is now the current managed data extension written into `data/`
- legacy `dpcache` is still accepted for backward compatibility
- temp imports create a real on-disk session project under app-local storage instead of failing for lack of a writable project path
- stale temp `Session_*` folders are purged on startup using the current seven-day cleanup rule
- `save as` updates the manifest path and project display name from the chosen filename stem
- named projects are saved on clean close
- source paths are stored relative to the manifest when they live under the project folder
- raw source references already record path, format, size, and modified time

### Not Implemented Yet

- a project is assigned a `.dlp` path immediately, but the `.dlp` file is not always written to disk at project creation time
- `.dlpd` filenames are still generated from internal source ids, not user-friendly source names
- multi-file import still loops through repeated single-file import logic
- the larger project-aware import dialog does not exist yet
- display-name vs filesystem-name sanitization is not implemented yet
- project-specific workspace/session layout is not fully stored in `.dlp`; some UI state still lives in global `QSettings`
- strong source fingerprints such as `sha256` are not populated yet
- the `ProjectStorage` regression test target currently fails and needs follow-up before this model can be considered fully locked down

## Core Model

One project equals one folder.

Each project folder contains:

- one lightweight project file: `.dlp`
- one managed data folder: `data/`

Example:

```text
<ProjectName>/
  ProjectName.dlp
  data/
```

## File Roles

### `.dlp`

The `.dlp` file is the main project file and the file the user opens.

It should remain relatively lightweight and store:

- project metadata
- references to imported raw datasets
- references to Dolphin-managed data files
- UI state
- layer state
- interpretation state
- processing state
- last session state

The session state is stored in the `.dlp` file.

The `.dlp` file should act as the lightweight project manifest that opens the project later.

Current implementation note:

- the manifest path is assigned when the project is created
- the `.dlp` file itself is currently written on explicit save, after successful import/reindex saves, after geodetic-settings changes, and on clean close for non-temp projects
- named projects therefore persist well, but a brand-new project is not guaranteed to have a physical `.dlp` file on disk until one of those save points occurs

The `.dlp` file should store:

- durable project state
- restorable workspace/session state

The `.dlp` file should not store:

- transient runtime objects
- in-flight import progress
- temporary caches
- logs
- large binary payloads
- other volatile state that only matters during the current process lifetime

### `.dlpd`

The `.dlpd` files are Dolphin Explorer managed binary data files stored in `data/`.

They hold Dolphin-managed project data derived from raw source files.

That data may include:

- extracted data
- normalized data
- indexed data
- parsed data
- processed data

They are intended to hold the heavier project data used for:

- fast project reopen
- visualization
- downstream processing

These files are not intended to be opened directly by the user.

Current implementation note:

- the extension is now `.dlpd`
- the file naming is still based on internal `source_id` values such as `src_<...>.dlpd`
- readable user-facing names such as `Tow-01.dlpd` are still a future improvement

Example:

```text
<ProjectName>/
  ProjectName.dlp
  data/
    Tow-01.dlpd
    Tow-02.dlpd
```

## Naming Rules

If the user provides a project name:

- use that name for the project folder
- use that name for the `.dlp` file

Example:

```text
Harbor Survey/
  Harbor Survey.dlp
  data/
```

If the user does not provide a project name:

- auto-generate a name using date and time
- optionally derive the base name from the first imported file

Example:

```text
Project_2026-04-09_10-42-15/
  Project_2026-04-09_10-42-15.dlp
  data/
```

The goal is that both the folder name and the main project file name remain understandable to the user.

### Display Name vs Filesystem Name

The user-facing project name and the filesystem-safe project name should be treated separately.

The app may preserve a friendly project display name in `.dlp`, while sanitizing the folder/file name written to disk.

Current implementation note:

- the display name is currently derived from the selected filename stem
- there is not yet a separate sanitization layer for filesystem-safe names

### Filesystem Sanitization Rules

When generating the project folder and `.dlp` filename, Dolphin Explorer should:

- remove or replace invalid filesystem characters such as `< > : " / \ | ? *`
- remove control characters
- trim trailing spaces
- trim trailing periods
- collapse repeated whitespace where appropriate
- reject reserved Windows device names such as `CON`, `PRN`, `AUX`, `NUL`, `COM1`-`COM9`, and `LPT1`-`LPT9`
- enforce a practical maximum length for the generated file stem
- resolve collisions by appending a suffix or timestamp

This allows:

- safe on-disk naming
- human-friendly display naming in the application

## Product Direction

The preferred direction is:

- one project equals one folder
- one lightweight `.dlp` file opens the whole project
- heavier Dolphin-managed binary data lives in `data/`
- user-facing project files use Dolphin Explorer branding
- internal cache naming such as `.dpcache` should not be exposed as the normal project model
- raw survey files remain untouched by default

This is intended to feel more like project-based survey software and less like a loose collection of files.

## Import Behavior

When the user imports data:

- Dolphin Explorer should ensure the project folder already exists before parsed data is written
- Dolphin Explorer should parse the source data
- Dolphin Explorer should save the parsed result into `data/` as `.dlpd`

By default, Dolphin Explorer should not rewrite or overwrite the original raw source file.

Instead:

- the raw source remains untouched
- Dolphin Explorer collects the data it needs from that source
- Dolphin Explorer stores the resulting managed project data in `.dlpd`

This means the normal project mode is reference-based for raw sources.

The `.dlp` file should store source references and source identity metadata such as:

- path
- format
- file size
- modified time
- source fingerprint when available

Parsed data should not be exposed to the user as `.dpcache`.

`.dpcache` reads as an internal cache implementation detail, while `.dlpd` reads as a Dolphin Explorer project data format.

## Import Workflow

Import should be treated as a project workflow, not as a simple file picker.

If a user selects one or more survey files, Dolphin Explorer should:

- decide the target project first
- create or open the project first
- then import the files into that project

If no project is open, the app should not fall into an ad hoc temporary-session path for normal import work.

Instead, it should:

- create or choose one real project target
- create the project folder
- create the `.dlp` file
- write managed imported data into that project

## Multi-File Import

Multi-file import should behave as one batch action.

That means:

- the user selects multiple files in one action
- the app resolves the project target once
- the app does not re-run project-creation logic separately for each file
- the app does not show a chain of small prompts for each file
- the app imports the batch into one organized project structure

Within the batch, each file should be classified as:

- reuse existing
- rebuild existing
- import new
- fail with a clear reason

The user should receive:

- per-file progress
- a clear batch-level summary

The batch rules should still respect source identity and duplicate handling per file.

## Import Dialog UX

The import UI should be a larger dedicated import dialog or workspace-style import window, not a small prompt box.

The purpose of that dialog is to let the user make project-level import decisions in one place.

It should show or configure:

- selected files
- project name
- project folder
- coordinate system / CRS
- whether the files are going into a new project, existing project, or current project
- duplicate handling behavior
- a clear summary of what will happen before import starts

The intended feel is:

- small dialog = simple file open
- large import dialog = project and survey import setup

For Dolphin Explorer, survey import should follow the second model.

Current implementation note:

- the current UX still uses a standard file picker followed by small modal prompts
- duplicate handling is still decided per file
- no large project-aware import workspace exists yet

## Save Behavior

The `.dlp` file should be created when the project is created.

The `.dlp` file should be updated:

- when the user saves
- when the software closes cleanly

This allows the `.dlp` file to act as the lightweight manifest and session carrier for the project.

Current implementation note:

- clean-close saving currently applies only to non-temp projects
- temp projects are intentionally not auto-saved on close
- some meaningful project state is persisted in `.dlp`, but project-specific workspace layout is not yet fully stored there

The `.dlp` file is intended to store both project state and last session state.

That means:

- enough state to restore the project meaningfully on reopen
- not so much transient state that `.dlp` becomes a runtime dump

## Organization Principles

- One project equals one folder.
- `.dlp` is the project entry point.
- `data/` stores Dolphin Explorer managed project data.
- User-facing project contents should use Dolphin branding, not internal cache naming.
- Large parsed data should be separated from the lightweight project manifest.
- Import should resolve project ownership before data-writing work begins.
- Multi-file import should be treated as one coordinated batch.
- The normal import path should feel organized and professional, not improvised.
- Raw source files remain untouched unless a future explicit archive mode is added.
- Display names and filesystem-safe names should be handled separately.

## Raw Source Policy

Raw source files are not the project and should not be modified during normal import.

The default policy is:

- raw files stay where they are
- `.dlp` references them
- `.dlpd` stores Dolphin-managed derived project data

This is the default reference mode.

A future archive mode may optionally copy raw files into a project-owned `raw/` folder, but that is not the normal default.

## Data Provenance

Each `.dlpd` file should be traceable back to the raw source or sources that produced it.

At minimum, Dolphin Explorer should be able to record:

- source path or source identifier
- source fingerprint when available
- import time
- Dolphin Explorer version or data-format version
- any important import configuration needed to determine rebuild or compatibility behavior

This is important for:

- rebuild decisions
- stale-data detection
- trust in project results
- long-term maintainability

Current implementation note:

- `path`, `format`, `size`, and `modified time` are already recorded for sources
- a `sha256` field exists in the project model, but it is not yet populated during import
- stale-data decisions currently rely on file presence, size, modified time, and cache validity checks

## Open Gaps

The highest-priority remaining gaps in this model are:

- finish the failing `ProjectStorage` regression coverage
- decide whether new projects should write the initial `.dlp` immediately on creation
- move multi-file import from repeated single-file prompts to one coordinated batch workflow
- replace the small import prompts with a larger import dialog that exposes project folder, CRS, and duplicate-handling decisions
- implement display-name vs filesystem-safe-name sanitization and collision handling
- decide exactly which workspace/session settings belong in `.dlp` and move them out of global `QSettings` where appropriate
- replace opaque `source_id`-based `.dlpd` names with cleaner user-facing data-file names
- populate stronger source provenance such as `sha256` when needed

## Optional Future Folders

If needed later, the project folder can also include:

- `exports/`
- `reports/`
- `backup/`

Example:

```text
<ProjectName>/
  ProjectName.dlp
  data/
  exports/
  reports/
  backup/
```

## Summary

The preferred Dolphin Explorer project model is:

- `.dlp` for the lightweight project and session file
- `.dlpd` for heavier Dolphin-managed project data derived from raw sources
- one folder per project
- no user-facing `.dpcache`
- one deliberate project-oriented import workflow
- one larger import dialog for project/import decisions
- multi-file import handled as one batch into one project
- raw source files left untouched by default
- explicit separation between display names and filesystem-safe names

This model is clean, portable, scalable, and aligned with the direction of professional project-based survey software.
