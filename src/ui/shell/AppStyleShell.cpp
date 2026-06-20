#include "ui/shell/AppStylePrivate.h"

namespace dolphin::ui::detail {

QString qssShell()
{
    return QString(

        // ---------------------------------------------------------------------
        // Shell structure
        // ---------------------------------------------------------------------

        "#shellRoot { background: @bg; }"

        // Activity bar (left)
        "#activityBar {"
        "  background: @bg;"
        "  border-right: 1px solid @border;"
        "}"
        // Qt does not support the CSS 'opacity' property in stylesheets.
        // Use background-based state feedback instead.
        "#activityBar QToolButton {"
        "  background: rgba(255,255,255,0.04); border: none;"
        "  border-radius: 8px; padding: 0; margin: 1px 4px;"
        "}"
        "#activityBar QToolButton:hover   { background: rgba(255,255,255,0.10); }"
        "#activityBar QToolButton:checked { background: rgba(@accentRgb,0.22); }"

        // Right tool bar
        "#toolBar {"
        "  background: @bg;"
        "  border-left: 1px solid @border;"
        "}"
        "#toolBar QToolButton {"
        "  background: rgba(255,255,255,0.04); border: none;"
        "  border-radius: 8px; padding: 0; margin: 1px 4px;"
        "}"
        "#toolBar QToolButton:hover   { background: rgba(255,255,255,0.10); }"
        "#toolBar QToolButton:checked { background: rgba(@accentRgb,0.22); }"

        // Right tool bar dividers
        "QFrame#toolbarDivider {"
        "  background: @border; margin: 4px 8px; max-height: 1px; border: none;"
        "}"

        // Context panel (left overlay). The right-edge divider is a dedicated
        // #shellDivider widget (a QSS border on this QStackedWidget is unreliable —
        // its page paints over it).
        "#contextPanel {"
        "  background: @bgPanel;"
        "}"
        // 1px panel divider line (left File Explorer right edge).
        "#shellDivider { background: @border; }"
        "#panelHdr {"
        "  background: @bgEl;"
        "  border-bottom: 1px solid @border;"
        "  min-height: 38px; max-height: 38px;"
        "}"
        "#panelTitle {"
        "  color: @textSoft; font-family: @font; font-size: @fontXs; font-weight: 600;"
        "  letter-spacing: 0.8px;"
        "}"
        // Edge toggle tabs — sit between the side panels and the main viewport
        "#leftEdgeStrip, #rightEdgeStrip {"
        "  background: @bgPanel;"
        "}"
        "QToolButton#panelEdgeBtnLeft {"
        "  background: @bgEl; border: 1px solid @border; border-left: none;"
        "  border-top-right-radius: 5px; border-bottom-right-radius: 5px;"
        "  color: @textMuted; font-family: @font; font-size: @fontSm; padding: 0;"
        "}"
        "QToolButton#panelEdgeBtnLeft:hover { background: @bgHover; color: @textSoft; }"
        "QToolButton#panelEdgeBtnLeft:pressed { background: @bgEl; }"
        "QToolButton#panelEdgeBtnRight {"
        "  background: @bgEl; border: 1px solid @border; border-right: none;"
        "  border-top-left-radius: 5px; border-bottom-left-radius: 5px;"
        "  color: @textMuted; font-family: @font; font-size: @fontSm; padding: 0;"
        "}"
        "QToolButton#panelEdgeBtnRight:hover { background: @bgHover; color: @textSoft; }"
        "QToolButton#panelEdgeBtnRight:pressed { background: @bgEl; }"

        // Placeholder body text in empty panel pages
        "QLabel#panelPlaceholder {"
        "  color: @textDim; font-family: @font; font-size: @fontBase;"
        "  padding: 18px 14px;"
        "}"

        // Main area
        "#mainArea { background: @bg; }"

        // Floating layer picker
        "#layerPicker { background: @bgEl; border: 1px solid @border; border-radius: 8px; }"
        "#layerPickerHeader { border-radius: 8px; }"
        "#layerPickerHeader:hover { background: rgba(255,255,255,0.04); }"
        "#layerPickerBody { background: @bgEl; }"
        "#layerPickerBody QLineEdit {"
        "  background: @overlayEl;"
        "  border: 1px solid @overlayHov;"
        "  border-radius: 5px; color: @textSecond;"
        "  font-size: @fontSm; padding: 3px 8px;"
        "}"
        "#layerPickerBody QTreeWidget {"
        "  background: transparent; border: none; color: @textSecond; font-size: @fontBase; outline: none;"
        "}"
        "#layerPickerBody QTreeWidget::item { padding: 3px 6px; border-radius: 5px; }"
        "#layerPickerBody QTreeWidget::item:hover { background: @overlayMut; }"
        "#layerPickerBody QTreeWidget::item:selected { background: rgba(@accentRgb,0.25); color: @textPrimary; }"
        "#layerPickerBody QScrollBar:vertical { background: transparent; width: 6px; }"
        "#layerPickerBody QScrollBar::handle:vertical {"
        "  background: rgba(255,255,255,0.18); border-radius: @radius1; min-height: 24px;"
        "}"
        "#layerPickerBody QScrollBar::add-line:vertical,"
        "#layerPickerBody QScrollBar::sub-line:vertical { height: 0; }"

        // Layer picker title label
        "QLabel#layerPickerTitle {"
        "  color: @textSecond; font-size: @fontXs; font-family: @font;"
        "  font-weight: 700; letter-spacing: 1.2px; background: transparent;"
        "}"

        // -- Map viewport overlay buttons (2D/3D toggle + terrain load) ---------
        // Floating pill buttons overlaid on the map canvas — dark semi-transparent
        // background so they read against both sea and terrain imagery.
        "QToolButton#map3DBtn {"
        "  background: @overlayEl; border: 1px solid rgba(255,255,255,0.14);"
        "  border-radius: @radius2;"
        "  color: @textSecond; font-family: @font; font-size: @fontSm; padding: @padXs @padSm;"
        "}"
        "QToolButton#map3DBtn:hover   { background: @overlayHov; color: @textPrimary; }"
        "QToolButton#map3DBtn:pressed { background: @overlayMut; }"

        "QToolButton#map3DTerrainBtn {"
        "  background: @overlayEl; border: 1px solid rgba(255,255,255,0.14);"
        "  border-radius: @radius2;"
        "  color: @textSecond; font-family: @font; font-size: @fontSm; padding: @padXs @padSm;"
        "}"
        "QToolButton#map3DTerrainBtn:hover    { background: @overlayHov; color: @textPrimary; }"
        "QToolButton#map3DTerrainBtn:pressed  { background: @overlayMut; }"
        "QToolButton#map3DTerrainBtn:disabled { color: @textDisabled; }"

        "QLabel#map3DTerrainLabel {"
        "  color: @textSubtle; font-family: @font; font-size: @fontXs;"
        "  background: @overlayMut; border-radius: @radius1; padding: @padXs @padSm;"
        "}"

        // Import-hint CTA button — accented pill, centred over the empty map canvas.
        "QPushButton#mapImportHintBtn {"
        "  background: rgba(@accentRgb,0.18); color: @textPrimary;"
        "  border: 1px solid rgba(@accentRgb,0.40); border-radius: @radius2;"
        "  font-family: @font; font-size: @fontBase; padding: 8px 20px;"
        "}"
        "QPushButton#mapImportHintBtn:hover {"
        "  background: rgba(@accentRgb,0.30); border-color: @accent;"
        "}"
        "QPushButton#mapImportHintBtn:pressed { background: rgba(@accentRgb,0.42); }"

        // Properties panel (right dock)
        "#propertiesPanel {"
        "  background: @bgPanel;"
        "  border-left: 1px solid @border;"
        "}"
        // Scroll area inside Properties panel — same treatment as waterfall panels
        "#propertiesPanel QScrollArea { background: transparent; border: none; }"
        "#propertiesPanel QScrollArea > QWidget { background: @bgPanel; border: none; }"
        "#propertiesPanel QScrollArea > QWidget > QWidget { background: @bgPanel; }"
        "#propertiesPanel QScrollArea QScrollBar:vertical {"
        "  background: transparent; width: 5px; border: none; margin: 0;"
        "}"
        "#propertiesPanel QScrollArea QScrollBar::handle:vertical {"
        "  background: rgba(255,255,255,0.14); border-radius: 2px; min-height: 20px;"
        "}"
        "#propertiesPanel QScrollArea QScrollBar::handle:vertical:hover {"
        "  background: rgba(255,255,255,0.26);"
        "}"
        "#propertiesPanel QScrollArea QScrollBar::add-line:vertical,"
        "#propertiesPanel QScrollArea QScrollBar::sub-line:vertical { height: 0; }"

        // -- Properties/Controls splitter handle -------------------------------
        // Must be styled explicitly — without this rule Qt's CSS engine makes the
        // handle transparent and it stops receiving mouse events entirely.
        "QSplitter#propsSplitter::handle:vertical {"
        "  background: @border;"
        "  height: 1px;"
        "  margin: 3px 0;"
        "}"
        "QSplitter#propsSplitter::handle:vertical:hover {"
        "  background: rgba(@accentRgb,0.5);"
        "}"
        "QSplitter#propsSplitter::handle:vertical:pressed {"
        "  background: @accentSoft;"
        "}"

        // ---------------------------------------------------------------------
        // Main toolbar
        // ---------------------------------------------------------------------

        "QToolBar#mainToolBar {"
        "  background: @bgEl; border: none;"
        "  border-bottom: 1px solid @border;"
        "  spacing: 1px; padding: 4px 10px;"
        "}"
        "QToolBar#mainToolBar::separator {"
        "  width: 1px; background: @borderMenu; margin: 7px 5px;"
        "}"
        "QToolBar#mainToolBar QToolButton {"
        "  background: transparent; border: none;"
        "  border-radius: @radius3; padding: 6px 7px;"
        "  min-width: 30px;"
        "}"
        "QToolBar#mainToolBar QToolButton:hover   { background: @overlayHov; }"
        "QToolBar#mainToolBar QToolButton:pressed { background: rgba(255,255,255,0.04); }"
        "QToolBar#mainToolBar QToolButton:checked { background: rgba(@accentRgb,0.22); }"
        "QToolBar#mainToolBar QToolButton:disabled { background: transparent; }"
        "QToolBar#mainToolBar QToolButton::menu-indicator { image: none; }"

        // ---------------------------------------------------------------------
        // Menu bar + menus
        // ---------------------------------------------------------------------

        "QMenuBar {"
        "  background: @bgEl; color: @textSecond;"
        "  font-family: @font; font-size: @fontSm;"
        "  border-bottom: 1px solid @border; padding: @padXs @padSm;"
        "}"
        "QMenuBar::item { background: transparent; padding: 4px 10px; border-radius: 5px; }"
        "QMenuBar::item:selected { background: @overlayHov; }"
        "QMenuBar::item:pressed  { background: @overlayMut; }"
        "QPushButton#menuOverflowBtn {"
        "  background: transparent; border: none; border-radius: 5px;"
        "  color: @textSecond; font-family: @font; font-size: @fontSm;"
        "  padding: 4px 8px;"
        "}"
        "QPushButton#menuOverflowBtn:hover   { background: @overlayHov; }"
        "QPushButton#menuOverflowBtn:pressed { background: @overlayMut; }"
        "QMenu {"
        "  background: @bgCard; border: 1px solid @borderMenu; border-radius: 10px;"
        "  color: @textPrimary; font-family: @font; font-size: @fontSm; padding: 6px 0;"
        "}"
        "QMenu::item { padding: 6px 32px 6px 16px; border-radius: @radius3; }"
        "QMenu::item:selected { background: rgba(@accentRgb,0.3); }"
        "QMenu::separator { height: 1px; background: @borderMenu; margin: 4px 0; }"

        // ---------------------------------------------------------------------
        // Status bar
        // ---------------------------------------------------------------------

        "QStatusBar {"
        "  background: @bgEl; border-top: 1px solid @border;"
        "  color: @textMuted; font-family: @font; font-size: @fontSm; padding: 0 8px;"
        "}"
        "QStatusBar::item { border: none; }"

        "QLabel#statusChrome {"
        "  color: @textMuted; font-family: @font; font-size: @fontSm;"
        "}"
        "QLabel#statusData {"
        "  color: @textSoft; font-family: @font; font-size: @fontSm;"
        "  padding: 0 10px 0 0;"
        "}"

        // Status bar: field name labels + bordered value boxes
        "QLabel#statusFieldLabel {"
        "  color: @textMuted; font-family: @font; font-size: @fontSm;"
        "  padding: 0 3px 0 10px;"
        "}"
        "QLabel#statusValueBox {"
        "  background: @bgCard; border: 1px solid @border; border-radius: 3px;"
        "  color: @textSecond; font-family: @font; font-size: @fontSm;"
        "  padding: 1px 7px;"
        "}"
        // Clickable CRS badge — same chrome as statusValueBox but interactive
        "QPushButton#statusCrsBtn {"
        "  background: @bgCard; border: 1px solid @border; border-radius: 3px;"
        "  color: @textSecond; font-family: @font; font-size: @fontSm;"
        "  padding: 1px 7px; text-align: center;"
        "}"
        "QPushButton#statusCrsBtn:hover  { background: rgba(@accentRgb,0.15); border-color: rgba(@accentRgb,0.45); color: @textPrimary; }"
        "QPushButton#statusCrsBtn:pressed { background: rgba(@accentRgb,0.25); }"
        "QPushButton#statusCrsBtn:disabled { color: @textMuted; }"

        // Status bar: interactive spin boxes (scale, rotation).
        // Text and arrow colors are set via QPalette in code; QSS only handles layout/chrome.
        "QDoubleSpinBox#statusSpinBox {"
        "  background: @bgCard;"
        "  border: 1px solid rgba(255,255,255,0.12);"  // visible on dark status bar
        "  border-radius: 3px;"
        "  font-family: @font; font-size: @fontSm;"
        "  padding: 1px 2px 1px 6px; min-width: 70px;"
        "}"
        "QDoubleSpinBox#statusSpinBox::up-button {"
        "  subcontrol-origin: border; subcontrol-position: top right;"
        "  width: 14px; border-left: 1px solid rgba(255,255,255,0.10);"
        "  border-top-right-radius: 3px;"
        "  background: rgba(255,255,255,0.05);"
        "}"
        "QDoubleSpinBox#statusSpinBox::down-button {"
        "  subcontrol-origin: border; subcontrol-position: bottom right;"
        "  width: 14px; border-left: 1px solid rgba(255,255,255,0.10);"
        "  border-bottom-right-radius: 3px;"
        "  background: rgba(255,255,255,0.05);"
        "}"
        "QDoubleSpinBox#statusSpinBox::up-button:hover,"
        "QDoubleSpinBox#statusSpinBox::down-button:hover {"
        "  background: rgba(@accentRgb,0.28);"
        "}"
        "QDoubleSpinBox#statusSpinBox::up-arrow   { image: url(:/icons/spin_up.svg);   width: 7px; height: 5px; }"
        "QDoubleSpinBox#statusSpinBox::down-arrow { image: url(:/icons/spin_down.svg); width: 7px; height: 5px; }"


        // AI provider icon — color driven by [aiProvider] dynamic property.
        // Property values are role names ("primary", "integration"), not brand names.
        "QLabel#aiIcon { font-family: @font; font-size: @fontSm; }"
        "QLabel#aiIcon[aiProvider=\"primary\"]     { color: @aiProviderPrimary; }"
        "QLabel#aiIcon[aiProvider=\"integration\"] { color: @aiIntegration; }"

        // AI status dot — background driven by [aiStatus] dynamic property
        "QLabel#aiDot { border-radius: @radius1; }"
        "QLabel#aiDot[aiStatus=\"offline\"] { background: @textMuted; }"
        "QLabel#aiDot[aiStatus=\"ready\"]   { background: @success; }"
        "QLabel#aiDot[aiStatus=\"active\"]  { background: @caution; }"

        // ---------------------------------------------------------------------
        // Title bar search + window control buttons
        // ---------------------------------------------------------------------

        // -- Unified command / Conversation / Assistant pill ----------------------
        "QFrame#uniBar {"
        "  background: @overlayEl;"
        "  border: 1px solid @overlayHov;"
        "  border-radius: 8px;"
        "  min-height: 26px; max-height: 26px;"
        "}"
        "QFrame#uniBar:hover {"
        "  background: @overlayHov;"
        "  border-color: rgba(255,255,255,0.13);"
        "}"
        "QFrame#uniBar[commandActive=\"true\"] {"
        "  background: @overlayHov;"
        "  border-color: rgba(@accentRgb,0.6);"
        "}"
        // Search field inside the pill — transparent, no own border
        "QFrame#uniBar QLineEdit#titleSearch {"
        "  background: transparent; border: none; border-radius: 0;"
        "  color: @textSecond; font-family: @font; font-size: @fontBase; padding: 0 8px;"
        "  selection-background-color: rgba(@accentRgb,0.4);"
        "}"
        "QFrame#uniBar QLineEdit#titleSearch:hover { background: transparent; border: none; }"
        "QFrame#uniBar QLineEdit#titleSearch:focus { background: transparent; border: none; }"
        // Thin inner separators between search / Conversation / Assistant
        "QFrame#uniBarSep { background: rgba(255,255,255,0.12); border: none; }"

        "QPushButton#winBtnStd {"
        "  background: transparent; border: none;"
        "  color: @iconStroke; font-size: @fontLg; font-family: @font;"
        "}"
        "QPushButton#winBtnStd:hover { background: rgba(255,255,255,0.10); color: @textPrimary; }"
        "QPushButton#winBtnClose {"
        "  background: transparent; border: none;"
        "  color: @iconStroke; font-size: @fontLg; font-family: @font;"
        "}"
        "QPushButton#winBtnClose:hover   { background: @closeBtnHover; color: @textPrimary; }"
        "QPushButton#winBtnClose:pressed { background: rgba(196,43,28,0.80); color: @textPrimary; }"

        // -- Nav arrow buttons (back / forward) --------------------------------
        "QPushButton#navBtn {"
        "  background: transparent; border: none; border-radius: @radius2;"
        "  color: @textDisabled; font-family: @font; font-size: 16px; font-weight: 600; padding: 0;"
        "}"
        "QPushButton#navBtn:enabled          { color: @textSubtle; }"
        "QPushButton#navBtn:enabled:hover    { background: rgba(255,255,255,0.09); color: @textPrimary; }"
        "QPushButton#navBtn:enabled:pressed  { background: @overlayMut; }"

        // -- Buttons inside the unified pill ----------------------------------
        // Conversation — icon-only, no text padding
        "QPushButton#uniBarBtn {"
        "  background: transparent; border: none; border-radius: 5px;"
        "  color: @textMuted; padding: 0;"
        "}"
        "QPushButton#uniBarBtn:hover   { background: @overlayHov; }"
        "QPushButton#uniBarBtn:pressed { background: rgba(255,255,255,0.04); }"
        // Assistant — accent
        "QPushButton#uniBarBtnAccent {"
        "  background: transparent; border: none; border-radius: 5px;"
        "  color: @accentSoft; font-family: @font; font-size: @fontSm; padding: 0 8px;"
        "}"
        "QPushButton#uniBarBtnAccent:hover   { background: rgba(@accentRgb,0.10); color: @textPrimary; }"
        "QPushButton#uniBarBtnAccent:pressed { background: rgba(@accentRgb,0.06); }"

        // -- Layout panel toggle buttons (corner widget, left of min/max/close) --
        // Match win-control button treatment exactly — same size, same hover colours.
        "QPushButton#layoutToggleBtn {"
        "  background: transparent; border: none;"
        "  color: @iconStroke;"
        "}"
        "QPushButton#layoutToggleBtn:hover   { background: rgba(255,255,255,0.10); color: @textPrimary; }"
        "QPushButton#layoutToggleBtn:pressed { background: rgba(255,255,255,0.04); }"
        "QPushButton#layoutToggleBtn:checked { background: @overlayHov; }"
        "QPushButton#layoutToggleBtn:checked:hover { background: rgba(255,255,255,0.14); }"

        // 1-px background-colour line — more reliable than QFrame::VLine
        "QFrame#cornerSep { background: @border; border: none; }"

        // Corner widget must explicitly match the menu bar surface
        "QWidget#titleBarCorner { background: @bgEl; }"

        // -- Conversation panel ------------------------------------------------
        "QFrame#convPanel {"
        "  background: @bgPanel;"
        "  border: 1px solid @border;"
        "  border-radius: 10px;"
        "}"
        "QWidget#convHeader {"
        "  background: @bgEl;"
        "  border-radius: 10px 10px 0 0;"
        "}"
        "QLabel#convDot   { color: @accentSoft; font-size: @fontSm; }"
        "QLabel#convTitle { color: @textSecond; font-family: @font; font-size: @fontBase; font-weight: 600; }"
        "QPushButton#convCloseBtn {"
        "  background: transparent; border: none; border-radius: @radius2;"
        "  color: @textMuted; font-size: @fontSm;"
        "}"
        "QPushButton#convCloseBtn:hover { background: @overlayHov; color: @textSecond; }"
        "QFrame#convSep { background: @border; border: none; }"
        "QScrollArea#convScroll { background: transparent; border: none; }"
        "QScrollArea#convScroll > QWidget > QWidget { background: @bgPanel; }"
        "QWidget#convMessages { background: @bgPanel; }"
        // User bubble — right-aligned, accent tint
        "QLabel#convBubbleUser {"
        "  background: rgba(@accentRgb,0.18); color: @textPrimary;"
        "  font-family: @font; font-size: @fontBase; padding: 7px 11px;"
        "  border-radius: 10px 10px 2px 10px;"
        "}"
        // AI bubble — left-aligned, neutral surface
        "QLabel#convBubbleAI {"
        "  background: @overlayEl; color: @textSecond;"
        "  font-family: @font; font-size: @fontBase; padding: 7px 11px;"
        "  border-radius: 10px 10px 10px 2px;"
        "}"
        "QWidget#convInputRow { background: @bgEl; border-radius: 0 0 10px 10px; }"
        "QLineEdit#convInput {"
        "  background: @overlayEl; border: 1px solid @overlayHov;"
        "  border-radius: @radius3; color: @textSecond;"
        "  font-family: @font; font-size: @fontBase; padding: 0 8px;"
        "}"
        "QLineEdit#convInput:focus { border-color: rgba(@accentRgb,0.40); }"
        "QPushButton#convSendBtn {"
        "  background: rgba(@accentRgb,0.20); border: none; border-radius: @radius3;"
        "  color: @accentSoft; font-size: 14px;"
        "}"
        "QPushButton#convSendBtn:hover   { background: rgba(@accentRgb,0.30); color: @textPrimary; }"
        "QPushButton#convSendBtn:pressed { background: rgba(@accentRgb,0.12); }"

        // -- Embedded panel chat (right panel Chats tab) -----------------------
        "QWidget#panelChat { background: @bgPanel; }"
        // Header
        "QWidget#panelChatHdr { background: @bgEl; border-bottom: 1px solid @border; }"
        "QLabel#panelChatIcon  { color: @accentSoft; font-size: @fontMd; padding: 0 2px 0 0; }"
        "QLabel#panelChatTitle { color: @textSecond; font-family: @font; font-size: @fontBase; font-weight: 600; }"
        "QPushButton#panelChatNewBtn {"
        "  background: @overlayMut; border: 1px solid @overlayHov; border-radius: @radius2;"
        "  color: @textSubtle; font-family: @font; font-size: @fontXs; padding: 0 7px;"
        "}"
        "QPushButton#panelChatNewBtn:hover   { background: @overlayEl; color: @textSecond; }"
        "QPushButton#panelChatNewBtn:pressed { background: @overlayMut; }"
        // Model selector combo
        "QComboBox#panelChatModelCombo {"
        "  background: @overlayMut; border: 1px solid @overlayHov; border-radius: @radius2;"
        "  color: @textSubtle; font-family: @font; font-size: @fontXs;"
        "  padding: 0 18px 0 7px; text-align: left; min-width: 56px;"
        "}"
        "QComboBox#panelChatModelCombo:hover  { background: @overlayEl; color: @textSecond; border-color: @overlayActive; }"
        "QComboBox#panelChatModelCombo:focus  { border-color: rgba(@accentRgb,0.40); }"
        "QComboBox#panelChatModelCombo::drop-down { border: none; width: 16px; subcontrol-origin: padding; subcontrol-position: right center; }"
        "QComboBox#panelChatModelCombo QAbstractItemView {"
        "  background: @bgCard; border: 1px solid @borderMenu;"
        "  color: @textPrimary; selection-background-color: rgba(@accentRgb,0.25);"
        "  outline: none; padding: 2px 0; font-family: @font; font-size: @fontSm;"
        "}"
        // Sep + scroll
        "QFrame#panelChatSep { background: @border; border: none; }"
        "QScrollArea#panelChatScroll { background: transparent; border: none; }"
        "QScrollArea#panelChatScroll > QWidget > QWidget { background: @bgPanel; }"
        "QWidget#panelChatMessages { background: @bgPanel; }"
        // Empty state
        "QWidget#panelChatEmpty { background: transparent; }"
        "QLabel#panelChatEmptyIcon  { color: @accentSoft; font-size: 22px; }"
        "QLabel#panelChatEmptyTitle { color: @textSecond; font-family: @font; font-size: @fontMd; font-weight: 600; }"
        "QLabel#panelChatEmptySub   { color: @textDim; font-family: @font; font-size: @fontSm; }"
        // Suggestion chips
        "QWidget#panelChatChips { background: transparent; }"
        "QPushButton#panelChatChip {"
        "  background: @overlayMut; border: 1px solid @overlayHov; border-radius: @radius3;"
        "  color: @textSubtle; font-family: @font; font-size: @fontXs; padding: 6px 8px;"
        "  text-align: left;"
        "}"
        "QPushButton#panelChatChip:hover   { background: @overlayEl; color: @textSecond; border-color: @overlayActive; }"
        "QPushButton#panelChatChip:pressed { background: @overlayMut; }"
        // Input container
        "QFrame#panelChatInputBox {"
        "  background: @bgEl;"
        "  border-top: 1px solid @border;"
        "}"
        "QTextEdit#panelChatInput {"
        "  background: @overlayEl; border: 1px solid @overlayHov; border-radius: @radius3;"
        "  color: @textSecond; font-family: @font; font-size: @fontBase; padding: 4px 8px;"
        "  selection-background-color: rgba(@accentRgb,0.25);"
        "}"
        "QTextEdit#panelChatInput:focus { border-color: rgba(@accentRgb,0.40); }"
        "QLabel#panelChatHint { color: @textDim; font-family: @font; font-size: @fontXxs; }"
        "QPushButton#panelChatSendBtn {"
        "  background: rgba(@accentRgb,0.20); border: none; border-radius: @radius3;"
        "  color: @accentSoft; font-size: 14px; font-weight: 600;"
        "}"
        "QPushButton#panelChatSendBtn:enabled:hover   { background: rgba(@accentRgb,0.35); color: @textPrimary; }"
        "QPushButton#panelChatSendBtn:enabled:pressed { background: rgba(@accentRgb,0.12); }"
        "QPushButton#panelChatSendBtn:disabled { background: @overlayMut; color: @textDim; }"

    );
}

} // namespace dolphin::ui::detail
