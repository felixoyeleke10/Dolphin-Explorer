# Dolphin Explorer

Dolphin Explorer is a desktop application for importing, viewing, and processing
marine sonar data. It provides map, waterfall, sub-bottom, metadata, contact, and
node-graph workflows in a native Qt interface.

> **Project status:** Dolphin Explorer is under active development. File formats,
> workflows, and project compatibility may change before a stable release.

## Highlights

- Imports XTF, JSF, and SEG-Y sonar data
- Organizes source files into projects and interpreted layers
- Uses index-first, visible-first loading for responsive large-file workflows
- Displays data through map, waterfall, and sub-bottom views
- Supports contact management and geospatial tools
- Includes a node-graph processing pipeline for correction, filtering,
  enhancement, analysis, merging, and output
- Persists project manifests as `.dlp` files and parsed artifacts as `.dlpd`
  files

## Requirements

Dolphin Explorer currently builds on Windows with:

- Visual Studio with the MSVC C++ toolchain
- CMake 3.20 or newer
- Ninja
- Qt 6 with Core, Gui, Widgets, OpenGL, OpenGLWidgets, Concurrent, Svg, and
  Network components

The `build_mingw` directory name is retained for historical reasons; the primary
build uses MSVC, not MinGW or GCC.

## Build

For the first build, run:

```bat
build_mingw.bat
```

This initializes the Visual Studio environment, configures CMake, and builds the
application in `build_mingw/`.

For subsequent incremental builds:

```bat
build_quick.bat
```

Alternatively:

```bat
cd build_mingw
cmake --build . --parallel
```

## Run

```bat
launch.bat
```

The launch script prepares the required Qt runtime libraries and starts
`DolphinExplorer.exe`.

## Test

After building, run the complete test suite with:

```bat
cd build_mingw
ctest --output-on-failure
```

Individual test executables can also be run directly from `build_mingw/tests/`.
Real vendor-data fixtures used by the tests are stored in `tests/fixtures/`.

## Architecture

The codebase follows a strict dependency direction:

```text
core
  |
  v
util / io / geo / render
  |
  v
pipeline
  |
  v
app
  |
  v
ui
```

- `src/core` contains the header-only data model.
- `src/io` contains format detection, readers, indexes, decoding, and parsed
  artifact caching.
- `src/geo` and `src/render` provide geospatial and rendering support.
- `src/pipeline` implements the processing DAG and graph runner.
- `src/app` owns projects, layers, imports, services, and background workers.
- `src/ui` contains Qt widgets, feature modules, and the application composition
  root.

## Repository layout

```text
cmake/       CMake support scripts
resources/   Application resources
src/         Production source code
tests/       Automated tests and fixtures
```

## License

No license has been published for this repository. Unless a license is added,
all rights are reserved by the copyright holder.
