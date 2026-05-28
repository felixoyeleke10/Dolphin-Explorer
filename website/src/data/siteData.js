export const featuredStories = [
  {
    id: "mission-01",
    label: "Software",
    readTime: "04 min read",
    title: "Desktop survey pipeline for XTF, JSF, SEG-Y, and parsed DLPD data",
    summary:
      "Dolphin Explorer imports, indexes, caches, maps, and reviews marine survey data without re-reading raw files on every project open.",
    tone: "deep",
    art: { pattern: "contour", scheme: "ocean" },
    link: "/dolphin-explorer",
    linkLabel: "View software",
  },
  {
    id: "mission-02",
    label: "Research brief",
    readTime: "05 min read",
    title: "GPU-ready inference for near-real-time anomaly screening offshore",
    summary:
      "Inference stays close to the operator with models shaped for tight turnaround, sparse bandwidth, and vessel-side review loops.",
    tone: "signal",
    art: { pattern: "target", scheme: "steel" },
    link: "/research",
    linkLabel: "Read research",
  },
  {
    id: "mission-03",
    label: "Platform",
    readTime: "03 min read",
    title: "Project-first workflow keeps parsed survey data durable",
    summary:
      "Named projects use a .dlp manifest with durable .dlpd parsed data, duplicate-source checks, and clear rebuild/reuse behavior.",
    tone: "orbit",
    art: { pattern: "nodes", scheme: "ink" },
    link: "/changelog",
    linkLabel: "See changelog",
  },
];

export const researchUpdates = [
  {
    kicker: "Workflow update",
    title: "Import-once behavior now drives project reopen and cache reuse",
    body:
      "Dolphin Explorer treats parsed survey data as a durable workflow asset. Existing projects and valid .dlpd caches are reused before new parsing work is started.",
    art: { pattern: "timeline", scheme: "steel" },
  },
  {
    kicker: "Validation update",
    title: "CTest coverage now spans parsers, storage, georeferencing, and GL startup",
    body:
      "Nine automated tests cover cancellation, task registry behavior, parsed cache, project storage, sidescan georeferencing, nav corrections, XTF, SEG-Y, and OpenGL smoke startup.",
    art: { pattern: "nodes", scheme: "ink" },
  },
  {
    kicker: "Viewer update",
    title: "2D and 3D map views share the same survey layer state",
    body:
      "The map viewport now switches between 2D swath review and an OpenGL 3D scene with survey outlines, nav layers, terrain loading, HUD overlays, and shared layer selection.",
    art: { pattern: "panel", scheme: "ocean", label: "Node Graph" },
  },
];

export const productHighlights = [
  "Project-oriented .dlp workflow with durable .dlpd parsed survey data",
  "Import-once workflow with duplicate-source checks, cache reuse, and rebuild paths",
  "XTF, JSF, SEG-Y, DLPD, and legacy DPCACHE reader support",
  "Sidescan waterfall with real scrollbar, band selection, seabed tracking, and metadata",
  "2D/3D map viewport with swath rendering, nav tracks, CRS tools, and terrain loading",
  "Visual node graph pipeline with 19 processing nodes",
  "SSS and SBP metadata windows with charts, copy, and CSV export",
  "Nine CTest targets covering parser, storage, georef, task, and GL smoke behavior",
];

export const labStats = [
  {
    kicker: "Focus",
    value: "Ocean-first",
    label: "Survey, mapping, and offshore intelligence",
    art: { pattern: "contour", scheme: "ocean" },
  },
  {
    kicker: "Engine",
    value: "GPU + AI",
    label: "Native acceleration from sensor to insight",
    art: { pattern: "stack", scheme: "steel" },
  },
  {
    kicker: "Platform",
    value: "Desktop + field",
    label: "Close to the data, close to the operator",
    art: { pattern: "target", scheme: "slate" },
  },
];

export const missionNotes = [
  {
    kicker: "Review loop",
    title: "Triage must reduce fatigue, not add another dashboard",
    body:
      "Mission systems need to surface likely events quickly while preserving access to the full survey timeline and original context.",
    art: { pattern: "target", scheme: "steel" },
  },
  {
    kicker: "Operations",
    title: "Field conditions shape the software as much as the model",
    body:
      "Sparse bandwidth, long watches, and inconsistent file quality matter just as much as algorithm quality when the system is used offshore.",
    art: { pattern: "contour", scheme: "ink" },
  },
  {
    kicker: "Interpretation",
    title: "Analysts need continuity across projects, previews, and decisions",
    body:
      "The best tooling keeps project state, visual review flow, and interpretation notes connected rather than scattering them across unrelated systems.",
    art: { pattern: "nodes", scheme: "slate" },
  },
];

export const productDetails = [
  {
    kicker: "Import workflow",
    title: "Import once, reopen existing work, rebuild only when needed",
    body:
      "The import path is built around source identity, project lookup, cache validation, and clear reuse or rebuild decisions before duplicate parsing begins.",
    art: { pattern: "timeline", scheme: "ocean" },
  },
  {
    kicker: "Project storage",
    title: "Project-oriented .dlp model with durable .dlpd parsed survey data",
    body:
      "Named projects use a .dlp manifest, with parsed survey data written to .dlpd files inside the project data folder and validated by source fingerprint plus cache version.",
    art: { pattern: "stack", scheme: "steel" },
  },
  {
    kicker: "Viewers",
    title: "Waterfall, map, 3D, metadata, and sub-bottom review in one shell",
    body:
      "Long sidescan lines can be reviewed in a scrollable waterfall, mapped as swaths, inspected in metadata tables, or sent through the node graph pipeline.",
    art: { pattern: "panel", scheme: "ink", label: "Waterfall" },
  },
  {
    kicker: "Formats",
    title: "Parser coverage spans sidescan and sub-bottom survey sources",
    body:
      "XTF and JSF support sidescan workflows, SEG-Y supports sub-bottom traces, and parsed DLPD stores artifact indexes, nav, metadata, and decoded survey records.",
    art: { pattern: "target", scheme: "steel" },
  },
  {
    kicker: "Quality",
    title: "Automated checks are now part of the product surface",
    body:
      "CTest validates cancellation, task registry behavior, project storage, parsed cache, georeferencing, nav corrections, XTF, SEG-Y, and OpenGL startup.",
    art: { pattern: "nodes", scheme: "ocean", label: "CTest" },
  },
];

export const labPrinciples = [
  {
    kicker: "Ground truth",
    title: "Operational realism over presentation-only demos",
    body:
      "We design systems around messy acquisition conditions, incomplete metadata, and real interpretation pressure instead of idealized showcase datasets.",
    art: { pattern: "nodes", scheme: "ink" },
  },
  {
    kicker: "Interface",
    title: "Software should clarify the mission, not compete with it",
    body:
      "The interface has to stay legible and disciplined even when the data is dense, the survey is long, and the operator is tired.",
    art: { pattern: "timeline", scheme: "slate" },
  },
  {
    kicker: "Context",
    title: "Marine data comes first, with broader sensing when useful",
    body:
      "Space and earth-observation products matter when they strengthen marine understanding, but they should serve the mission rather than distract from it.",
    art: { pattern: "orbit", scheme: "steel" },
  },
];

export const changelogEntries = [
  {
    id: "cl-2026-05-c",
    date: "May 2026",
    version: "Current build",
    tags: ["Validation", "Release", "Quality"],
    title: "Build and test gate covers parser, storage, georef, and GL startup paths",
    body:
      "The app now has a real CMake/CTest validation floor for the core desktop workflow, plus a production website build that can be checked independently.",
    items: [
      "CTest target set: CancellationToken, TaskRegistry, ParsedCache, ProjectStorage, SidescanGeoref, NavCorrections, XtfReader, SegyReader, GlSmoke",
      "GL smoke test verifies WaterfallView and MapView3D startup without shader initialization errors",
      "SEG-Y and XTF parser tests use reproducible in-code fixtures instead of external binary dependencies",
      "Website production build passes with the local Node/Vite toolchain",
    ],
    art: { pattern: "target", scheme: "steel" },
    link: "/dolphin-explorer",
  },
  {
    id: "cl-2026-05-b",
    date: "May 2026",
    version: "Import once",
    tags: ["Import", "Projects", "Cache"],
    title: "Import-once workflow treats parsed survey data as durable project state",
    body:
      "Dolphin Explorer now presents import as a registration and reuse workflow instead of a repeated raw-file parse. Project lookup, duplicate detection, source fingerprints, and cache validity drive the decision.",
    items: [
      "Valid .dlpd parsed cache can be reused without decoding raw files again",
      "Stale or missing parsed cache rebuilds against the existing layer path",
      "Existing source detection prevents duplicate project and layer creation paths from becoming normal workflow",
      "Local workspace policy keeps this first pass deterministic without claiming a network registry",
    ],
    art: { pattern: "timeline", scheme: "ocean" },
    link: "/dolphin-explorer",
  },
  {
    id: "cl-2026-05-a",
    date: "May 2026",
    version: "Viewer expansion",
    tags: ["Map", "3D", "Metadata"],
    title: "2D/3D map viewport and richer metadata windows are wired into the shell",
    body:
      "The main viewport now supports 2D sidescan map review and an OpenGL 3D mode, while SSS and SBP metadata windows provide table, chart, copy, and CSV export workflows.",
    items: [
      "MapViewportHost owns the 2D/3D stack and forwards layer state into MapView3D",
      "3D mode includes survey outline, nav layers, terrain loading, HUD labels, scale bar, and compass overlays",
      "SSS metadata loads nav-focused ping data asynchronously",
      "SBP metadata supports trace review, charts, line filtering, copy, and CSV export",
    ],
    art: { pattern: "orbit", scheme: "ink" },
    link: "/dolphin-explorer",
  },
  {
    id: "cl-2025-04-c",
    date: "April 2025",
    version: "Phase 1.5",
    tags: ["Architecture", "Build"],
    title: "Complete codebase modularisation across all six libraries",
    body:
      "Every monolithic source file has been split into focused compilation units. The project now follows a clean module hierarchy with no circular dependencies.",
    items: [
      "Project split into project/, services/, layers/ subdirectories under app/",
      "io/ reorganised into xtf/, jsf/, and cache/ format subdirectories",
      "pipeline/nodes/ categorised into input/, correction/, filter/, enhancement/, analysis/, merge/, output/",
      "ui/views/ split into map/, waterfall/, and nodegraph/ view groups",
      "XtfNavTable extracted to standalone XtfNav.h/cpp — lazy GPS interpolation decoupled from reader lifecycle",
      "MapTypes.h, LayerUtils.h extracted to break heavy include chains",
    ],
    art: { pattern: "nodes", scheme: "ink" },
    link: null,
  },
  {
    id: "cl-2025-04-b",
    date: "April 2025",
    version: "Phase 1.4",
    tags: ["Pipeline", "Node Graph"],
    title: "Visual node graph pipeline with 19 processing nodes",
    body:
      "A full drag-and-connect processing canvas lets analysts build signal chains without writing code. Nodes cover the complete path from raw data input through correction, filtering, enhancement, and export.",
    items: [
      "DataIn: SidescanInput, SBPInput, MBESInput — source nodes for all three modalities",
      "Correction: TVG, BandPass, BottomDetect, SlantRange, GeoCorrect, NavSmooth",
      "Filter: ClipRange, SpeckleFilter, Decimat",
      "Enhancement: GainNormalize, ContrastEnhance, HistogramEq",
      "Analysis: ShadowDetect",
      "Merge: MergeNode (2-input A+B), MultiMergeNode (2–8 configurable ports)",
      "Output: ExportGeoTiff, ExportCsv",
      "Interactive canvas with pan, zoom, port drag-connect, and node inspector",
    ],
    art: { pattern: "nodes", scheme: "ocean" },
    link: "/dolphin-explorer",
  },
  {
    id: "cl-2025-04-a",
    date: "April 2025",
    version: "Phase 1.3",
    tags: ["UI", "MainWindow"],
    title: "MainWindow modularised into 13 focused coordinators",
    body:
      "The application shell has been split into domain-specific coordinators. Each file now owns a single concern — layout, menus, toolbar, geodesy, waterfall wiring, node graph wiring, and so on.",
    items: [
      "MainWindow.Shell.cpp — header, activity bar, panel host lifecycle",
      "MainWindow.LayerCoordinator.cpp — layer add/remove/select events",
      "MainWindow.WaterfallCoordinator.cpp — waterfall open/close/sync",
      "MainWindow.NodeGraphCoordinator.cpp — node graph window lifecycle",
      "MainWindow.Geodesy.cpp — CRS picker and projection wiring",
      "InspectorPanel split into LayerInspectorPage and ContactInspectorPage",
      "LineListPanel.Tree.cpp extracted for tree-building logic",
    ],
    art: { pattern: "stack", scheme: "steel" },
    link: null,
  },
  {
    id: "cl-2025-03-b",
    date: "March 2025",
    version: "Phase 1.2",
    tags: ["Waterfall", "Viewer"],
    title: "Sidescan waterfall viewer with real scroll, seabed tracking, and overlays",
    body:
      "The waterfall view renders full XTF surveys at 2 px per ping with a live scrollbar, amplitude colour mapping, seabed auto-detection, and a multi-panel inspector.",
    items: [
      "WaterfallRenderer + WaterfallRendererRow — tile-based ping rendering",
      "WaterfallSeabedTracker — automatic bottom pick with manual override",
      "SeabedAutoDetector — energy-weighted first-return algorithm",
      "WaterfallAmpProfile — per-column amplitude histogram overlay",
      "WaterfallOverlayPainter — HUD, amplitude curve, and annotation layers",
      "WaterfallInspectorPanel + WaterfallAnalysisPanel with image, seabed, and contact tabs",
      "WaterfallScrollSync — keeps multiple open waterfall windows in sync",
    ],
    art: { pattern: "contour", scheme: "ocean" },
    link: "/dolphin-explorer",
  },
  {
    id: "cl-2025-03-a",
    date: "March 2025",
    version: "Phase 1.1",
    tags: ["Import", "Project", "Storage"],
    title: "Project system, import queue, and .dlpd parsed cache",
    body:
      "Named projects use a .dlp manifest. Parsed survey data is written to .dlpd cache files so reopening a project skips raw file decoding entirely.",
    items: [
      "ImportDialog — per-file action control (reuse / rebuild / skip) before parsing starts",
      "Serial import queue — one-at-a-time parsing with live progress overlay",
      "ImportProgressOverlay — non-blocking status panel for active and completed files",
      "ParsedCache — binary .dlpd format stores artifact index, nav fixes, and sonar metadata",
      "Project CRUD split across project/, services/, and layers/ modules",
      "File menu: New, Open, Save, Save As, Open Folder, Close Project, and import flows",
    ],
    art: { pattern: "timeline", scheme: "steel" },
    link: null,
  },
  {
    id: "cl-2025-02-b",
    date: "February 2025",
    version: "Phase 1.0",
    tags: ["IO", "XTF", "JSF", "Nav"],
    title: "XTF and JSF format readers with GPS interpolation",
    body:
      "Binary readers for the two most common marine sonar formats. Both readers build a full artifact index in a single pass and interpolate GPS positions for pings that carry zero coordinates.",
    items: [
      "XtfReader — lifecycle, index, payload decode, nav interpolation",
      "XtfNavTable — standalone GPS fix store with linear interpolation",
      "XtfIndex — single-pass artifact indexer with coordinate backfill",
      "XtfPayload — per-artifact sample decode with amplitude normalisation",
      "JsfReader — JSF format support (EdgeTech systems)",
      "AmplitudeScale — 16-bit and 32-bit normalisation with 99th-percentile clipping",
      "IFormatReader — shared interface for all future format readers",
    ],
    art: { pattern: "target", scheme: "ink" },
    link: null,
  },
  {
    id: "cl-2025-02-a",
    date: "February 2025",
    version: "Phase 0.9",
    tags: ["Map", "Geodesy", "CRS"],
    title: "MapView with sidescan swath rendering and CRS-aware geodesy",
    body:
      "The map view renders sidescan coverage as positioned swath strips over a nav track. Coordinate reference system detection handles both geographic and projected survey data.",
    items: [
      "MapView — OpenGL-accelerated swath strip renderer with per-layer toggle",
      "MapViewPaint — tile compositing and amplitude-to-colour mapping",
      "SidescanSwathGeoreferencer — projects sonar samples from slant range to geographic position",
      "GeoUtils — UTM ↔ WGS84 conversions and bounding-box helpers",
      "EpsgDatabase — lookup table for 600+ common CRS codes",
      "CrsPickerDialog — searchable EPSG picker with projected/geographic filter",
      "NavSmoother — Kalman-based GPS track smoother for noisy vessel positions",
    ],
    art: { pattern: "orbit", scheme: "steel" },
    link: null,
  },
];
