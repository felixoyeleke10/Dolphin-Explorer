# Dolphin Explorer

Dolphin Explorer is a native Windows desktop application for importing,
organizing, visualizing, processing, and exporting marine survey data. It is
written in C++20 with Qt 6 and is designed around large sonar files that should
remain usable without decoding an entire survey before the first view appears.

The application combines project and layer management with 2D/3D mapping,
sidescan waterfall review, sub-bottom interpretation, contact management,
geodesy tools, raster support, and a node-based processing pipeline.

> **Development status**
>
> Dolphin Explorer is under active development and has not reached a stable
> release. Interfaces, file compatibility, and saved project structures may
> evolve. The current supported development environment is Windows with MSVC.

## Contents

- [Capabilities](#capabilities)
- [Supported data](#supported-data)
- [How data flows through the application](#how-data-flows-through-the-application)
- [Project files and parsed artifacts](#project-files-and-parsed-artifacts)
- [Requirements](#requirements)
- [Dependency setup](#dependency-setup)
- [Build](#build)
- [Run](#run)
- [Test](#test)
- [Architecture](#architecture)
- [Processing pipeline](#processing-pipeline)
- [Repository layout](#repository-layout)
- [Development conventions](#development-conventions)
- [Troubleshooting](#troubleshooting)
- [Current limitations](#current-limitations)
- [License](#license)

## Capabilities

### Survey import and organization

- Automatically probes supported survey files before import.
- Classifies available modalities and creates one or more project layers from a
  source file.
- Prevents duplicate import using normalized absolute path plus file size and
  modification-time identity.
- Builds indexes before full sample hydration so large files can become visible
  quickly.
- Prioritizes visible data and performs additional decoding in the background.
- Supports cancellation and progress reporting for long-running import and
  processing jobs.
- Limits heavy import/decode work to two concurrent jobs by default.

### Visualization and interpretation

- 2D/3D map display for survey tracks, sonar coverage, contacts, and
  georeferenced rasters.
- Sidescan waterfall display with palette and radiometric controls.
- Sub-bottom profile display and correction tools.
- Metadata inspection for imported sources and layers.
- Contact creation, review, snapshots, and measurements.
- Data-library and layer-management workflows.
- Coordinate reference system and geodesy utilities.

### Processing and export

- Directed acyclic graph processing with validation, topological execution,
  dirty tracking, and JSON serialization.
- Sidescan correction, filtering, enhancement, and analysis nodes.
- Navigation smoothing and repair workflows.
- GeoTIFF output for georeferenced elevation or imagery products.
- PNG export for map/application views.
- CSV output through the processing graph.

## Supported data

| Format | Extensions | Primary use | Notes |
| --- | --- | --- | --- |
| XTF | `.xtf` | Sidescan, sub-bottom, and magnetometer data | Uses XTF Rev. 40 structures; a mixed file may produce multiple layers. |
| EdgeTech JSF | `.jsf` | Sidescan and sub-bottom data | Message Type 80 data is supported; compressed JSF samples are not currently decoded. |
| SEG-Y | `.segy`, `.sgy` | Sub-bottom profiles | Treated as a single-channel sub-bottom source in the application. |
| Dolphin parsed artifact | `.dlpd` | Durable indexed/decoded survey data | Can be imported directly; legacy `.dpcache` files are accepted on read. |
| GeoTIFF | Common GDAL-supported TIFF variants | Georeferenced image or elevation raster | Includes tiled, compressed, and BigTIFF inputs supported by GDAL. |
| Georeferenced image | PNG/JPEG plus world file | Map raster | World files such as `.pgw`, `.jgw`, and `.wld` provide georeferencing. |

Raster I/O is backed by GDAL. Coordinate transforms and CRS handling use PROJ.
Availability of a particular raster codec can depend on the GDAL build installed
through vcpkg.

## How data flows through the application

```text
Raw survey or raster
        |
        v
Format probe and modality classification
        |
        v
Index and spatial extents
        |
        v
Project source + one or more layers
        |
        +----> visible-first decode ----> map / waterfall / sub-bottom view
        |
        +----> background refinement ---> durable .dlpd artifact
        |
        +----> processing graph --------> derived artifact / export
```

This separation is intentional. Activating a layer should not require full-file
decoding. The index and extents establish what the dataset contains and where it
belongs; detailed samples are loaded according to what the user is viewing.

## Project files and parsed artifacts

Dolphin Explorer separates lightweight project organization from durable parsed
survey data:

- `.dlp` is the JSON project manifest. It stores project-level organization,
  sources, layers, contacts, and serialized workflow state.
- `.dlpd` is the parsed artifact store. It contains indexed/decoded data used by
  the application and is a durable workflow asset, not a disposable cache.
- `.dpcache` is a legacy artifact extension accepted for backward-compatible
  reads.

For an imported source, project-owned artifacts are normally placed below a
`data/` directory beside the project manifest. Some derived layer data may use
sidecar `.dlpd` files. Processing publishes replacements atomically and does not
modify the original XTF or JSF source.

Keep the `.dlp` manifest and its associated project data together when moving or
backing up a project. Deleting `.dlpd` files may remove parsed or processed work
that is expensive or impossible to reproduce exactly.

## Requirements

The primary build is tested around the following Windows toolchain:

- Windows 10 or 11, 64-bit
- Visual Studio 2022 or a compatible newer installation with **Desktop
  development with C++**
- CMake 3.20 or newer
- Ninja, supplied by Visual Studio or Qt
- Qt 6.7.2 or 6.7.3, `msvc2019_64` kit
- vcpkg with 64-bit GDAL and its PROJ dependency
- Git

Qt components used by the project are Core, Gui, Widgets, OpenGL,
OpenGLWidgets, Concurrent, Svg, and Network.

The `build_mingw/` directory name is historical. The primary build invokes
Microsoft `cl.exe`; do not configure that directory with MinGW or GCC.

## Dependency setup

### 1. Install Qt

Use the Qt online installer and install one of the MSVC kits recognized by the
build script:

```text
C:\Qt\6.7.3\msvc2019_64
C:\Qt\6.7.2\msvc2019_64
```

The script looks for Ninja in Visual Studio first, then at:

```text
C:\Qt\Tools\Ninja\ninja.exe
```

### 2. Install GDAL with vcpkg

The build expects vcpkg under `%USERPROFILE%\vcpkg` and the `x64-windows`
triplet:

```bat
git clone https://github.com/microsoft/vcpkg %USERPROFILE%\vcpkg
%USERPROFILE%\vcpkg\bootstrap-vcpkg.bat
%USERPROFILE%\vcpkg\vcpkg install "gdal[core,png,jpeg]:x64-windows"
```

GDAL supplies raster drivers; PROJ and supporting libraries are installed as
dependencies. The launch workflow copies the required DLLs and GDAL/PROJ data
beside the executable.

## Build

### Primary build: MSVC + Ninja

From a regular Command Prompt or PowerShell window at the repository root:

```bat
build_mingw.bat
```

The script:

1. Refuses to build while `DolphinExplorer.exe` is running.
2. Locates a supported Visual Studio installation.
3. Locates Qt 6.7.x and Ninja.
4. Initializes the 64-bit MSVC environment with `vcvars64.bat`.
5. Configures a Debug build with CMake.
6. Builds into `build_mingw/`.
7. Runs `windeployqt` for the executable.

After the first configure, use the faster incremental command:

```bat
build_quick.bat
```

Or build directly after initializing an MSVC developer environment:

```bat
cd build_mingw
cmake --build . --parallel
```

### Alternative Visual Studio/MSBuild build

An alternative configuration is available through:

```bat
build.bat
```

This configures and builds into `build\Debug\`. The MSVC + Ninja workflow above
is the primary supported developer path.

## Run

After a successful primary build:

```bat
launch.bat
```

The script deploys available GDAL and PROJ runtime files into `build_mingw/` and
starts `build_mingw\DolphinExplorer.exe`.

Create or open a `.dlp` project from the initial project interface, then import
supported survey data through the import workflow or by dragging a file onto the
map view.

## Test

Build the project first, then run all CTest-registered tests:

```bat
cd build_mingw
ctest --output-on-failure
```

Run a focused test by name:

```bat
cd build_mingw
ctest -R ParsedCache --output-on-failure
```

Test executables can also be launched directly from `build_mingw\tests\`.

The suite covers XTF, JSF, and SEG-Y readers; parsed artifact and raster I/O;
project storage, contacts, and imports; node graphs and processing; navigation
and georeferencing; waterfall and sub-bottom algorithms; map geometry and raster
draping; cancellation and task tracking; and OpenGL smoke checks. Real
vendor-data samples used by tests live in `tests/fixtures/`.

## Architecture

Dependencies move downward through the layer model. A lower layer must never
depend on a layer above it.

```text
core                         Header-only survey and spatial data model
  |
  v
util / io / geo / render    Parsing, caching, geodesy, algorithms, rendering
  |
  v
pipeline                     Processing DAG and node implementations
  |
  v
app                          Projects, layers, imports, services, workers
  |
  v
ui                           Qt features and application composition
```

### Core

`src/core/` defines shared structures including sidescan pings, sub-bottom
traces, navigation points, contacts, artifact indexes, spatial references,
magnetometer samples, and raster grids.

### I/O, geospatial, and rendering

`src/io/` owns format probing, binary readers, indexing, decoding, parsed
artifact serialization, and GDAL raster I/O. Format implementations are split
into probe, index, and decode stages so opening a dataset does not imply eager
decoding.

`src/geo/` contains CRS handling, coordinate conversion, and sonar
georeferencing. `src/render/` contains display-oriented sonar and color logic.
Neither layer depends on application or UI code.

### Pipeline

`src/pipeline/` provides the node registry, graph structure, traversal,
serialization, dirty tracking, contracts, and topological runner. It operates
without depending on the application or UI layers.

### Application

`src/app/` owns project state, sources, data layers, contacts, artifact paths,
import classification, asynchronous import, processing services, task tracking,
and workers. Business rules belong here rather than inside widgets.

### User interface

`src/ui/` is organized into self-contained features:

| Feature | Responsibility |
| --- | --- |
| `map` | 2D/3D spatial display, survey geometry, raster surfaces, and overlays |
| `waterfall` | Sidescan waterfall rendering, tools, and contact interaction |
| `subbottom` | Sub-bottom profile viewing and interpretation |
| `nodegraph` | Visual processing-graph editing |
| `processing` | Processing setup, progress, and execution UI |
| `import` | Project entry points and survey import setup |
| `metadata` | Source and layer metadata presentation |
| `geodesy` | Coordinate-system and geospatial workflows |
| `contacts` | Contact browsing, reporting, snapshots, and measurements |
| `datalibrary` | Project data and layer organization |
| `export` | Image, raster, and related export workflows |

Application-wide state and events are coordinated through `AppState`,
`ProjectEventBus`, and `WindowRegistry` under `src/ui/systems/`. The files under
`src/ui/mainwindow/` form the composition root; product policy should remain in
feature coordinators or application services instead of accumulating in
`MainWindow`.

## Processing pipeline

The processing graph groups nodes by role:

- **Input:** sidescan, sub-bottom, and multibeam inputs.
- **Correction:** slant-range correction, TVG, bottom detection, navigation
  smoothing, geocorrection, ARC, and band-pass operations.
- **Filter:** range clipping, decimation, and speckle filtering.
- **Enhancement:** gain normalization, contrast enhancement, histogram
  equalization, and sidescan enhancement.
- **Analysis:** acoustic-shadow detection.
- **Merge:** combines compatible processing branches.
- **Output:** GeoTIFF and CSV export nodes.

`GraphRunner` executes nodes in topological order. `NodeGraph` owns graph
structure, traversal, dirty propagation, and JSON serialization. Processing
outputs are published through application services so pipeline code remains
independent of project and UI concerns.

## Repository layout

```text
Dolphin-Explorer/
|-- cmake/                 CMake support and safety checks
|-- resources/             Icons and Qt resources
|-- src/
|   |-- core/              Shared data model
|   |-- util/              General utilities
|   |-- io/                Survey readers, raster I/O, parsed artifacts
|   |-- geo/               CRS and sonar geospatial algorithms
|   |-- render/            Rendering support
|   |-- pipeline/          Processing engine and nodes
|   |-- app/               Projects, layers, services, and workers
|   `-- ui/                Qt UI features and composition
|-- tests/                 Unit, integration, smoke, and performance tests
|-- CMakeLists.txt         Root CMake configuration
|-- build_mingw.bat        Primary configure/build script
|-- build_quick.bat        Incremental build helper
|-- build.bat              Alternative MSBuild workflow
`-- launch.bat             Runtime deployment and launcher
```

The CMake subdirectories are ordered as follows:

```text
util -> core -> io -> geo -> pipeline -> render -> app -> ui
```

## Development conventions

- Use C++20 and the existing Qt/CMake patterns.
- Preserve the strict dependency direction; `io` and `pipeline` must not depend
  on `app`, and `app` must not depend on `ui`.
- Keep business policy out of `MainWindow`; prefer coordinators or application
  services.
- Preserve index-first, visible-first loading. Do not require full-file decoding
  merely to activate a layer.
- Treat `.dlpd` files as durable artifacts rather than temporary caches.
- Keep unfinished UI actions hidden or disabled instead of shipping clickable
  placeholders.
- Maintain the default concurrency cap of two for heavy import/decode jobs unless
  a deliberate design change is made.
- Add or update focused tests when changing readers, project persistence,
  processing nodes, geospatial behavior, or UI policy.

Dataset identity is currently based on normalized absolute path plus file size
and modification-time fingerprint. It is not content-hash based.

## Troubleshooting

### Visual Studio is not found

Install the C++ workload and confirm that `vcvars64.bat` exists under a Visual
Studio 2022/compatible installation. The scripts search Community, Professional,
and Build Tools editions in their standard locations.

### Qt is not found

Install the `msvc2019_64` Qt 6.7.2 or 6.7.3 kit under `C:\Qt`, or update the local
build configuration if using a different compatible Qt layout.

### CMake cannot find GDAL or PROJ

Confirm that `%USERPROFILE%\vcpkg\installed\x64-windows` exists, rerun the vcpkg
install command from [Dependency setup](#dependency-setup), and reconfigure the
build directory.

### The executable is locked during build

Close Dolphin Explorer before compiling. The build includes a pre-link safety
check and the helper scripts also detect a running `DolphinExplorer.exe`.

### The application starts but raster CRS or drivers fail

Launch through `launch.bat` so the GDAL/PROJ DLLs and their data directories are
placed beside the executable. Check the vcpkg Debug binaries and `share\gdal` /
`share\proj` data.

### Clangd cannot resolve includes

The root `.clangd` uses `build_mingw/compile_commands.json`. Run
`build_mingw.bat` at least once; CMake export of compile commands is enabled by
default.

## Current limitations

- Windows/MSVC is the supported build path at present.
- There are no packaged binary releases yet.
- Compressed JSF sonar samples are not supported.
- SEG-Y is interpreted as single-channel sub-bottom data in the current product
  model.
- Legacy `.dpcache` artifacts are read for compatibility, but new parsed
  artifacts use `.dlpd`.
- The repository does not currently publish a license.

## License

No license has been published for this repository. Unless a license is added,
all rights are reserved by the copyright holder.
