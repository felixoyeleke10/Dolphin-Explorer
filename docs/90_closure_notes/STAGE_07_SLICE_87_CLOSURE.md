# Stage 07 · Slice 87 — Empty-map launcher + Views panel + draping surface

## Goal (user direction, SeaView reference)
1. A freshly opened app should offer actions on the empty canvas — Recent
   Projects + Import Files — instead of a bare "No survey loaded".
2. The left panel's Recent Projects section (now redundant) becomes a
   SeaView-style **Views** section (per-viewer tabs, per the reference shot).
3. The Views MAP tab hosts a **Draping surface** setting, giving 3D terrain a
   proper home instead of the 3D view asking for data ad-hoc.

## What changed

### Empty-map launcher (MapViewportHost)
- The empty-state overlay is now a launcher: "Import Files…" CTA + a solid
  "RECENT PROJECTS" card (up to 6 entries, click to open — deferred via
  singleShot(0), same Win32 blink guard as the old sidebar list).
- `setRecentProjects(names, paths)` + `openProjectRequested(path)` signal;
  MainWindow::refreshSidebarSections now feeds the launcher (all existing
  refresh call sites keep working).
- The painted "No survey loaded" line is gone; the "DOLPHIN EXPLORER"
  watermark moved up to sit above the launcher column.

### Views section (left panel, replaces Recent Projects)
- New `ViewsPanel` (mainwindow/panels/) inside a "Views" CollapsibleSection:
  PanelTabBar **MAP | SSS | SBP** over a stacked page set. MAG omitted — no
  magnetometer viewer exists yet (D-05: no dead tabs).
  - **MAP**: Palette (global mosaic palette → DisplayStateManager
    ::setMapPalette), Sonar preview (Off/Coverage only/Low/Medium/High →
    onMapSonarQuality), Draping surface (name + browse/clear).
  - **SSS / SBP**: palette override for the ACTIVE line of that modality
    (→ setLayerSssPalette / setLayerSbpPalette); disabled with a hint when no
    such line is active.
- Sync: displayStateChanged(Palette/MapQuality) → refreshViewsPanel();
  onLayerSelected and bindProjectUi also refresh. The panel is a dumb view —
  DisplayStateManager stays the single authority.
- The recent-list context actions (Remove from Recent / Clear All) now live
  only in File ▸ Recent Projects; the launcher entries are click-to-open.

### Draping surface (project-persisted 3D terrain)
- `Project::drapingSurface()/setDrapingSurface()` + `draping_surface` field in
  the .dlp manifest (absent when empty; older manifests read fine).
- `MainWindow::applyDrapingSurface` is the single mutate point: swaps the 3D
  terrain (MapViewportHost::loadTerrainPath/removeTerrainPath — file path =
  terrain layer id), persists to the project, updates the panel readout.
- Project open loads the stored surface automatically (existence-checked);
  project switch removes the previous project's terrain.
- The 3D HUD "⊞ Terrain" chip now also emits terrainFileLoaded → adopted as
  the project draping surface, so both entry points stay consistent.

## Verification
Build green; 16/16 tests. In-app widget grabs confirmed: launcher shows the
watermark + Import CTA + Recent Projects card (3 real projects listed);
left panel shows Views (MAP tab: Palette mirroring the live display state,
Sonar preview mirroring the current tier, Draping surface "None" + browse).

## Follow-ups
- Views SSS/SBP tabs can grow real per-viewer controls later (transparency /
  blend modes per the SeaView reference) once those exist in the render path.
- Launcher entries could show a right-click menu (Remove from Recent) if the
  File-menu path proves too hidden.
