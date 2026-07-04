# Stage 07 · Slice 81 — Chat → bottom dock; right-panel "Map" settings tab

## Goal (user direction)
1. The AI chat belongs with the other utility surfaces — move it into the
   bottom dock alongside Problems / Output / Jobs / Terminal.
2. The right panel's upper "Chats" tab becomes **"Map"**, hosting the general
   map settings for quick access.

## What changed

### Bottom dock: Chat tab
- `BottomDockPanel` grew a 5th tab ("Chat"): `kTabCount` 4→5, new
  `buildChatTab` page + **`setChatWidget(QWidget*)` injection API** — the dock
  layer must not link the mainwindow lib that owns `PanelChatWidget`, so
  MainWindow constructs the chat and injects it
  (`MainWindow.MainArea.cpp`, right after the dock is created).
- Saved tab state stays valid (indices 0–3 unchanged; Chat is 4).

### Right panel: "Map" page (redesigned per user direction)
- Upper tab bar is now **Properties | Map | History** (`m_props_tab_chats` →
  `m_props_tab_map`).
- First cut duplicated App Settings → Map (background/grid presets); user
  rejected that — the tab is now **map view working options** (SonarWiz-style):
  - **GENERAL** — *Show tooltips* (line label near the cursor while hovering
    coverage) and *Highlight items under cursor* (soft dashed accent outline on
    the hovered layer). New MapView backing: `setHoverTooltipsEnabled` /
    `setHoverHighlightEnabled` + `updateHoverState` (hit-test throttled to
    ≥6 px moves; Pan/Select modes only), hover outline painted after the
    selection outline, cleared on leave.
  - **CAMERA PROPERTIES** — *Azimuth* (0–359.9°, wrapping; routes to
    `MapViewportHost::setRotationDeg` → 2D canvas rotation or 3D yaw) and
    *Height/Depth* (m; converted with the host's own approximation
    `distance ≈ mpp · viewport_h / 2` so the spin and the camera agree).
    Both mirror live navigation via `viewportChanged` → `setCameraReadout`
    (skipped while the spin has focus so it never fights the user).
  - **MOSAIC SPOTLIGHT** — enable + radius (40–400 px): dims everything
    outside a feathered circle that follows the cursor (screen-space paint,
    drawn under the HUD so scale bar/badges stay readable).
- View options persist under QSettings `map/…`; camera values are session
  view state. `broadcastState()` pushes persisted options into the views at
  startup.
- `adjustPropsSplit`: the Map page is a compact form, so it sizes to content
  like Properties; only History keeps the lion's-share behaviour.

## Verification
Full build green; `ctest -E PerfBaseline` → 16/16 passed.

## Notes
- The title-bar "Conversation" pill (ConversationPanel) is a separate surface
  and is unchanged.
