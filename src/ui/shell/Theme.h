#pragma once

// Dolphin Explorer — UI theme constants
//
// AppStyle.cpp uses these tokens via applyTokens() substitution.
// All other widget code that needs a colour or geometry should reference
// these constants so the whole application can be re-skinned from one place.

namespace dolphin::ui::Theme {

// -- Font ---------------------------------------------------------------------
// Full fallback stack so the app renders correctly on macOS and Linux too.
inline constexpr auto kFontFamily = "'Segoe UI', 'SF Pro Text', -apple-system, sans-serif";
// CSS-formatted font size strings for AppStyle @token substitution.
// Leave 8, 14, 15, 17, 18 px as raw component-specific values.
inline constexpr auto kFontXxs = "9px";   // tiny badges / dense status labels
inline constexpr auto kFontXs  = "10px";  // captions / timestamp / secondary metadata
inline constexpr auto kFontSm  = "11px";  // body text — most common UI font size
inline constexpr auto kFontBase = "12px"; // inputs / buttons / section labels
inline constexpr auto kFontMd  = "13px";  // headings / primary toolbar labels
inline constexpr auto kFontLg  = "16px";  // icon-glyph labels in large toolbar buttons
// Monospace font size for the embedded terminal (QFont::setPointSize scale, not px).
inline constexpr int  kTerminalFontPt = 10;

// -- Background surfaces -------------------------------------------------------
inline constexpr auto kBg           = "#111113";  // deepest shell background
inline constexpr auto kBgElevated   = "#1c1c1e";  // toolbar / menu bar / status bar
inline constexpr auto kBgPanel      = "#161618";  // side panels / inspector / contact table
inline constexpr auto kBgCard       = "#2c2c2e";  // combo boxes / menus / cards

// -- Borders / dividers --------------------------------------------------------
inline constexpr auto kBorder       = "#2d2d2f";  // primary dividers
inline constexpr auto kBorderMenu   = "#38383a";  // menu borders / combo borders

// -- Accent --------------------------------------------------------------------
inline constexpr auto kAccent        = "#0a84ff";  // selection / active / primary button
inline constexpr auto kAccentHover   = "#1a94ff";  // accent hover state
inline constexpr auto kAccentPressed = "#0070d8";  // accent pressed state
inline constexpr auto kAccentSoft    = "#5abaff";  // accent-tinted text / gentle emphasis

// -- Text ----------------------------------------------------------------------
inline constexpr auto kTextPrimary   = "#f2f2f7";  // main body text
inline constexpr auto kTextSecond    = "#e5e5ea";  // secondary labels / list items
inline constexpr auto kTextSubtle    = "#8e8e93";  // subdued UI text / field helpers
inline constexpr auto kTextMuted     = "#636366";  // panel headers / chrome labels
inline constexpr auto kTextSoft      = "#98989d";  // tertiary metadata / coordinates
inline constexpr auto kTextDim       = "#3d5060";  // placeholder / empty state text
inline constexpr auto kTextDisabled  = "#444446";  // intentionally unavailable UI

// -- Icons ---------------------------------------------------------------------
inline constexpr auto kIconStroke    = "#aeaeb2";  // SVG icon stroke / tint

// -- Semantic ------------------------------------------------------------------
inline constexpr auto kSuccess           = "#4caf50";  // completed / healthy state
inline constexpr auto kDanger            = "#d13438";  // failed / destructive state
inline constexpr auto kDangerBright      = "#ff453a";  // iOS-style kill/terminate action
inline constexpr auto kDangerBrightHover = "#ff6961";  // hover state for kill/terminate
inline constexpr auto kWarning           = "#ffb900";  // caution / duplicate state
inline constexpr auto kCaution           = "#d18616";  // rebuild / attention state
// Pure white — for text on accent/colored button backgrounds only.
inline constexpr auto kWhite             = "#ffffff";

// -- AI integration colors ----------------------------------------------------
// Generic role names — the actual provider-to-role mapping lives in
// MainStatusBar.cpp so the design system stays brand-agnostic.
inline constexpr auto kAiProviderPrimary    = "#D97757";  // primary AI provider accent
inline constexpr auto kAiIntegrationAccent  = "#58A6FF";  // integration / secondary provider

// -- Raw RGB channels (for use inside rgba() expressions) ----------------------
// These allow @accentRgb to expand into "10,132,255" inside rgba(@accentRgb,0.3).
inline constexpr auto kAccentRgb     = "10,132,255";   // #0a84ff
inline constexpr auto kSuccessRgb    = "76,175,80";    // #4caf50
inline constexpr auto kDangerRgb     = "209,52,56";    // #d13438
inline constexpr auto kCautionRgb    = "209,134,22";   // #d18616

// -- Interaction overrides -----------------------------------------------------
inline constexpr auto kBgHover        = "#202022";  // hover tint on dark panel headers
inline constexpr auto kCloseBtnHover  = "#c42b1c";  // Windows Fluent close-button red

// -- Icon sizes (px, logical) --------------------------------------------------
inline constexpr int kIconSideBar = 16;  // activity bar + tool bar
inline constexpr int kIconToolBar = 16;  // top main toolbar

// -- Widget geometry (px, logical) --------------------------------------------
inline constexpr int kActivityBarW      = 42;
inline constexpr int kToolBarW          = 42;
inline constexpr int kContextPanelW     = 260;
inline constexpr int kPropertiesPanelW  = 240;
inline constexpr int kPanelHdrH         = 38;
inline constexpr int kSideButtonH       = 38;

// -- Command bar (search / command-palette trigger, all toolbars) --------------
inline constexpr int kCmdBarH        = 24;   // height in all toolbar contexts
inline constexpr int kCmdBarMinW     = 340;  // minimum width — search + Conversation + Assistant buttons
inline constexpr int kCmdBarMaxW     = 580;  // maximum width — keeps it from swamping narrow bars
inline constexpr int kCmdBarPct      = 28;   // target width as % of parent bar width
inline constexpr int kChromeConvBtnW = 28;   // Conversation button width in title bar pill

// -- Progress bar (thin activity stripe, all windows) -------------------------
inline constexpr int kProgressBarH = 4;  // height of the thin progress stripe

// -- Main window chrome --------------------------------------------------------
inline constexpr int kMainProgressBarW = 130; // status-bar progress indicator width
inline constexpr int kWinBtnW          = 46;  // min/max/close button width
inline constexpr int kWinBtnH          = 32;  // min/max/close button height
inline constexpr int kNavBtnW          = 30;  // back / forward nav button width
inline constexpr int kNavBtnH          = 24;  // back / forward nav button height
inline constexpr int kChromeActionH    = 22;  // Conversation / Assistant button height
inline constexpr int kLayoutBtnW       = 40;  // layout toggle buttons (corner widget)

// -- Main viewport overlay buttons (2D/3D toggle, terrain load) ---------------
inline constexpr int kMap3DBtnW    = 40;  // 2D↔3D toggle button width
inline constexpr int kMap3DBtnH    = 26;  // 2D↔3D toggle button height
inline constexpr int kMap3DMargin  = 10;  // margin from viewport edge for overlay buttons

// -- Acoustic viewer window geometry (WaterfallWindow + SubBottomWindow) ------
inline constexpr int kAvToolBarH      = 44;  // top toolbar height
inline constexpr int kAvBottomBarH    = 44;  // bottom status bar height
inline constexpr int kAvInspectorW    = 230; // left inspector panel width
inline constexpr int kAvAnalysisW     = 230; // right analysis / display panel width
inline constexpr int kAvScrollBarW    = 12;  // scroll bar width
inline constexpr int kAvQuickBtnSz    = 32;  // quick-action button (width = height)
inline constexpr int kAvProgressW     = 160; // bottom bar progress indicator width
// SSS-only (frequency selector lives only in WaterfallWindow toolbar)
inline constexpr int kWfFreqSelectorH = 26;  // frequency band combo box height
inline constexpr int kWfFreqSelectorW = 120; // frequency band combo box minimum width

// -- Spacing scale (px, logical) ----------------------------------------------
// Use for setContentsMargins() / setSpacing() — visual design decisions only.
// Zero is always written inline. Pure rendering / domain math may use any value.
inline constexpr int kSpacing1 = 4;   // tight: badge padding, dense button groups
inline constexpr int kSpacing2 = 6;   // standard widget / icon-to-text gap
inline constexpr int kSpacing3 = 8;   // loose widget spacing
inline constexpr int kSpacing4 = 12;  // section / content padding
inline constexpr int kSpacing5 = 16;  // comfortable panel margin
inline constexpr int kSpacing6 = 20;  // dialog / page outer padding
inline constexpr int kSpacing7 = 24;  // large section gap

// -- Border radius (px, logical) ----------------------------------------------
inline constexpr int  kRadius1 = 3;      // subtle: inputs, small badges, status dots
inline constexpr int  kRadius2 = 4;      // standard: cards, list items, tool buttons
inline constexpr int  kRadius3 = 6;      // prominent: dialogs, toasts, large panels
// CSS-formatted strings for AppStyle @token substitution
inline constexpr auto kRadius1Px = "3px";
inline constexpr auto kRadius2Px = "4px";
inline constexpr auto kRadius3Px = "6px";

// -- Separator / divider thickness --------------------------------------------
inline constexpr int kSepSz = 1;  // hairline separator (1px wide or tall)

// -- Small-widget button sizes (square, logical px) ----------------------------
// Use these whenever a QToolButton or small QPushButton needs setFixedSize().
inline constexpr int kSmallBtnSz   = 22;  // icon-only utility buttons (kill, clear, color picker)
inline constexpr int kMediumBtnSz  = 28;  // toolbar action buttons (close, collapse, undock)
inline constexpr int kSeabedBtnSz  = 34;  // seabed-pick action buttons in waterfall analysis panel

// -- Label key column widths ---------------------------------------------------
inline constexpr int kKeyLabelW    = 68;  // key column in right-panel / layer-inspector rows
inline constexpr int kToolbarSepH  = 18;  // VLine separator height inside icon toolbars

// -- Form / panel row heights --------------------------------------------------
// These keep input rows, color buttons, and section sub-headers visually aligned.
inline constexpr int kInputH     = 22;  // spin-boxes and line-edits inside form layouts
inline constexpr int kColorBtnH  = 24;  // color-picker buttons and sub-section headers
inline constexpr int kFormBtnH   = 26;  // action buttons inside form rows (Browse, Clear)
inline constexpr int kSubHdrH    = 24;  // panel sub-section / collapsible header strip
inline constexpr int kStatusH    = 20;  // selection / status indicator labels
inline constexpr int kPanelRowH  = 30;  // slider / spinbox content rows inside display panels
inline constexpr int kDialogBtnH = 32;  // primary action buttons in dialogs and panel nav rows
inline constexpr int kToolbarH   = 34;  // compact icon-only toolbar strips (metadata windows)
inline constexpr int kFilterBarH = 36;  // filter / navigation bar beneath toolbar

// -- 3D viewport --------------------------------------------------------------
// Grid line colours — passed from the CPU as GLSL uniforms so changing these
// tokens automatically re-skins the 3D ground grid without touching shaders.
inline constexpr auto kGrid3DMinor = "#425878";  // muted steel-blue minor lines
inline constexpr auto kGrid3DMajor = "#7099C2";  // brighter major lines

// Grid fade distances — multiples of camera distance used as near/far fade thresholds.
inline constexpr float kGrid3DFadeNearMult = 2.5f;
inline constexpr float kGrid3DFadeFarMult  = 7.0f;

// Survey outline line width (GL units).
inline constexpr float kSurveyOutlineWidth = 1.5f;

// -- Map viewport overlays (scale bar, HUD, compass) ---------------------------
// Drop-shadow stroke drawn one pixel below/right of overlay geometry.
inline constexpr int kMapOverlayShadowAlpha = 100;  // 0-255; ~40% black
// Text shadow drawn one pixel offset for HUD labels.
inline constexpr int kMapOverlayTextShadowAlpha = 150;

// -- Node graph — category accent colours (small category bar only) ------------
inline constexpr auto kNodeColorSource      = "#3A8A52";  // green   – source / data-in nodes
inline constexpr auto kNodeColorCorrection  = "#A06438";  // amber   – correction nodes
inline constexpr auto kNodeColorFilter      = "#2E62A8";  // blue    – filter nodes
inline constexpr auto kNodeColorEnhancement = "#6A40A0";  // purple  – enhancement nodes
inline constexpr auto kNodeColorAnalysis    = "#208078";  // teal    – analysis nodes
inline constexpr auto kNodeColorMerge       = "#7A5030";  // brown   – merge nodes
inline constexpr auto kNodeColorOutput      = "#903030";  // red     – output nodes
inline constexpr auto kNodeColorUnknown     = "#505868";  // grey    – unknown / default

// -- 3D viewport overlay: graticulae / HUD label geometry ---------------------
inline constexpr float kGratEdgeMarginH = 30.f;  // horizontal edge exclusion zone (px)
inline constexpr float kGratEdgeMarginV = 10.f;  // vertical edge exclusion zone (px)
inline constexpr int   kGratLabelPad    = 3;      // text padding inside label background
inline constexpr int   kGratLabelGap    = 4;      // gap between label edge and graticule line
inline constexpr int   kGratLabelFontPt = 8;      // point size for graticule labels

} // namespace dolphin::ui::Theme
