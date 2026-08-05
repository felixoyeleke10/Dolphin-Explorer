# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

## Build System

**Primary (MSVC + Ninja, output in `build_mingw/`):**
```bat
# First-time configure (run once — initializes VS environment via vcvars64):
build_mingw.bat

# Incremental build (after configure):
cd build_mingw && cmake --build . --parallel

# Or use the convenience script:
build_quick.bat
```

Note: the directory is named `build_mingw/` for legacy reasons; the compiler is
MSVC (`cl.exe`) invoked via vcvars64, not MinGW. Do not use MinGW/GCC toolchains.

**MSVC/MSBuild alternative:**
```bat
build.bat   # configures and builds into build\Debug\
```

**Run the app:**
```bat
launch.bat   # copies Qt runtime DLLs and launches DolphinExplorer.exe
```

**CMake build subdirectory order:** `util → core → io → geo → pipeline → render → app → ui`

## Tests

Tests live in `tests/` and are integrated into the CMake build:
```bat
cd build_mingw
ctest --output-on-failure
```

Run a single test executable directly from `build_mingw/tests/`. Test fixtures (real vendor data samples) live in `tests/fixtures/`.

## Architecture

### Layer Model (strict dependency direction)

```
core   (header-only data model: pings, contacts, spatial refs)
  ↓
util / io / geo / render   (parsing, geospatial, color — no App dependency)
  ↓
pipeline   (DAG processing engine — no App/UI dependency)
  ↓
app   (projects, layers, services, import, workers — no UI dependency)
  ↓
ui    (Qt widgets, features, mainwindow composition root)
```

A layer must never depend on anything above it. `src/io/` and `src/pipeline/` do not know about `src/app/`.

### Key Subsystems

**`src/io/`** — Format readers (XTF, JSF, SEG-Y) with a shared cache layer (`cache/ParsedCache.*`). `ProbeDispatch.cpp` handles auto-detection. Each format has reader + index + decode + probe files.

**`src/pipeline/`** — Node-graph processing engine. `NodeGraph.cpp` owns structure, traversal, dirty-tracking, and JSON serialization. `GraphRunner.cpp` executes topologically. Node types: Input / Correction / Filter / Enhancement / Analysis / Merge / Output.

**`src/app/`** — Business logic hub. The `Project` is split across six files (`Project.cpp`, `.h`, `.Sources.cpp`, `.Layers.cpp`, `.Contacts.cpp`, `.Serialization.Read/Write.cpp`). `ImportJobManager` + `ImportClassifier` own import-once deduplication logic. `ImportService` drives async loading with cancellation tokens.

**`src/ui/systems/`** — App-level singletons: `AppState`, `ProjectEventBus`, `WindowRegistry`. These are the event bus and shared state backbone — prefer signaling through them over direct coupling.

**`src/ui/mainwindow/`** — Composition root. `MainWindow.cpp` is split across 16+ aspect files (`*.Commands.cpp`, `*.Project.cpp`, `*.Import.cpp`, `*.Shell.cpp`, …) and `coordinators/` for per-feature coordinators. **Do not push new product policy into MainWindow**; extract into a coordinator or service instead.

**`src/ui/features/`** — Self-contained feature modules: `map/`, `waterfall/`, `subbottom/`, `nodegraph/`, `import/`, `processing/`, `metadata/`, `geodesy/`, `contacts/`, `datalibrary/`.

### Data / File Model

- Project manifests: `.dlp` (JSON). Parsed artifact stores: `.dlpd` (`.dpcache` accepted on read for legacy).
- Sources → Layers: one sonar file produces multiple layer interpretations.
- Loading policy: index/extents first, visible-first decode, background refinement second. Never require full-file decode for layer activation.

## Working Conventions

Key locked decisions to respect:
- **D-02** Dataset identity = normalized absolute path + size/mtime fingerprint (no SHA-256 yet).
- **D-04** Parsed artifacts are durable workflow assets — do not treat `.dlpd` files as throwaway.
- **D-05** Unfinished UI actions must be hidden/disabled — never leave clickable stub actions on the shipped surface.
- **D-06** Prefer index-first, visible-first loading over eager full-data hydration.
- **D-14** Default concurrency cap for heavy import/decode jobs is 2.

**Work in slices:** one behavioral goal, one file cluster, one test theme. Record the *why* in the commit message and, when it's durable cross-session knowledge (not just what changed), in memory — not in a standalone docs file. Prior planning docs and closure notes were intentionally cleared; don't recreate that pattern.

**Stop and ask** (do not guess) if the work would require deciding: content-hash vs. path identity, true cross-project shared artifacts, local vs. network registry, or shipping partial UI as if it works.

## IDE / Tooling

- Clangd uses the `build_mingw/` compilation database (`.clangd` at root).
- Export compile commands is enabled by default in CMake (`CMAKE_EXPORT_COMPILE_COMMANDS`).
- MOC predefs are disabled for MSVC compatibility — this is intentional.
