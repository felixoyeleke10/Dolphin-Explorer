#include "ui/shell/AppStylePrivate.h"

namespace dolphin::ui::detail {

QString qssPanels()
{
    return QString(

        // ---------------------------------------------------------------------
        // Inspector panel (PropertiesPanel / InspectorPanel)
        // ---------------------------------------------------------------------

        // ---------------------------------------------------------------------
        // Left panel empty state (no project open)
        // ---------------------------------------------------------------------

        "QPushButton#emptyStateActionBtn {"
        "  background: rgba(@accentRgb,0.10); color: @textPrimary;"
        "  border: 1px solid rgba(@accentRgb,0.25); border-radius: @radius2;"
        "  font-family: @font; font-size: @fontBase; padding: 7px 10px; text-align: left;"
        "}"
        "QPushButton#emptyStateActionBtn:hover {"
        "  background: rgba(@accentRgb,0.20); border-color: rgba(@accentRgb,0.50);"
        "}"
        "QPushButton#emptyStateActionBtn:pressed { background: rgba(@accentRgb,0.30); }"

        "QLabel#emptyStateIcon  { color: rgba(@accentRgb,0.55); font-size: 28px; padding-bottom: 4px; }"
        "QLabel#emptyStateTitle { color: @textPrimary;  font-family: @font; font-size: @fontMd;  font-weight: 600; }"
        "QLabel#emptyStateSub   { color: @textMuted;    font-family: @font; font-size: @fontBase; padding: 0 8px; }"

        "QFrame#emptyStateSep { background: @border; max-height: 1px; border: none; }"

        "QLabel#emptyStateRecentHdr {"
        "  color: @textMuted; font-family: @font; font-size: @fontXxs; font-weight: 600;"
        "  letter-spacing: 0.5px; padding: 6px 0 2px;"
        "}"

        "QListWidget#emptyStateRecentList {"
        "  background: transparent; border: none;"
        "  color: @textSecond; font-family: @font; font-size: @fontSm;"
        "}"
        "QListWidget#emptyStateRecentList::item { padding: 3px 2px; border-radius: @radius3; }"
        "QListWidget#emptyStateRecentList::item:hover { background: @overlayMut; }"
        "QListWidget#emptyStateRecentList::item:selected {"
        "  background: rgba(@accentRgb,0.12); color: @textPrimary;"
        "}"

        // ---------------------------------------------------------------------
        // Inspector panel (PropertiesPanel / InspectorPanel)
        // ---------------------------------------------------------------------

        "QLabel#inspectorEmpty {"
        "  color: @textDim; font-family: @font; font-size: @fontBase; padding: 16px;"
        "}"
        "QLabel#inspectorTitle {"
        "  color: @textPrimary; font-family: @font; font-size: @fontMd; font-weight: 600;"
        "}"
        "QLabel#inspectorMeta {"
        "  color: @textMuted; font-family: @font; font-size: @fontSm;"
        "}"
        "QLabel#inspectorValue {"
        "  color: @iconStroke; font-family: @font; font-size: @fontBase;"
        "}"

        // ---------------------------------------------------------------------
        // CollapsibleSection (right panel module headers)
        // ---------------------------------------------------------------------
        "QWidget#sectionHdr {"
        "  background: @bgEl; border-top: 1px solid @border;"
        "  min-height: 26px; max-height: 26px;"
        "}"
        "QWidget#sectionHdr:hover { background: rgba(@accentRgb,0.07); }"
        "QLabel#sectionTitle {"
        "  color: @textMuted; font-family: @font; font-size: @fontSm;"
        "  font-weight: 500; letter-spacing: 0.2px; background: transparent;"
        "}"
        "QWidget#sectionHdr:hover QLabel#sectionTitle { color: @textSecond; }"
        "QLabel#sectionArrow {"
        "  color: @textSubtle; font-family: @font; font-size: @fontXxs; background: transparent;"
        "}"
        "QWidget#sectionHdr:hover QLabel#sectionArrow { color: @textSecond; }"
        "QLabel#sectionBadge {"
        "  color: @textSubtle; font-family: @font; font-size: 8px; font-weight: 600;"
        "  background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.10);"
        "  border-radius: 2px; padding: 0px 3px; letter-spacing: 0.2px;"
        "}"

        // Locked (not applicable) state — header dims, hint replaces content.
        "QWidget#sectionHdr[locked=\"true\"] { background: @bgEl; }"
        "QWidget#sectionHdr[locked=\"true\"] QLabel#sectionTitle { color: @textDisabled; }"
        "QWidget#sectionHdr[locked=\"true\"] QLabel#sectionArrow { color: @textDisabled; }"
        "QWidget#sectionHdr[locked=\"true\"] QLabel#sectionBadge { color: @textDisabled; opacity: 0.5; }"
        "QLabel#sectionHint {"
        "  color: @textSubtle; font-family: @font; font-size: @fontXs; font-style: italic;"
        "  background: @bgPanel; padding: 6px 10px 8px 22px;"
        "}"

        // Inspector panel — section dividers + key-value rows
        "QFrame#inspSectionHdr {"
        "  background: @bgEl;"
        "  border-top: 1px solid @border; border-bottom: 1px solid @border;"
        "  min-height: 26px; max-height: 26px;"
        "}"
        "QLabel#inspSectionTitle {"
        "  color: @textMuted; font-family: @font; font-size: @fontSm;"
        "  font-weight: 500; letter-spacing: 0.2px;"
        "}"
        "QLabel#inspMetaKey {"
        "  color: @textSubtle; font-family: @font; font-size: @fontSm;"
        "}"
        "QLabel#inspMetaVal {"
        "  color: @iconStroke; font-family: @font; font-size: @fontSm; font-weight: 500;"
        "}"

        // ---------------------------------------------------------------------
        // Shared panel section label (Import panel, Contact list)
        // ---------------------------------------------------------------------

        "QLabel#sectionLabel {"
        "  color: @textMuted; font-family: @font; font-size: @fontXs;"
        "  font-weight: 600; letter-spacing: 0.4px;"
        "}"

        // ---------------------------------------------------------------------
        // Mini control panels (Nav, Heading, Gain, Imaging)
        // ---------------------------------------------------------------------

        // Gain / Imaging control widgets
        "QCheckBox#ctrlToggle {"
        "  color: @textSecond; font-family: @font; font-size: @fontSm; spacing: 5px;"
        "}"
        "QCheckBox#ctrlToggle::indicator {"
        "  width: 13px; height: 13px; border-radius: @radius1;"
        "  border: 1px solid rgba(255,255,255,0.18);"
        "  background: @overlayMut;"
        "}"
        "QCheckBox#ctrlToggle::indicator:hover   { border-color: rgba(255,255,255,0.32); }"
        "QCheckBox#ctrlToggle::indicator:checked { background: @accentSoft; border-color: @accentSoft; }"
        "QCheckBox#ctrlToggle::indicator:disabled { background: rgba(255,255,255,0.03); border-color: @overlayHov; }"
        "QCheckBox#ctrlToggle:disabled { color: @textDisabled; }"

        "QLabel#ctrlParamLabel {"
        "  color: @textSubtle; font-family: @font; font-size: @fontXs;"
        "}"
        "QDoubleSpinBox#ctrlSpinBox, QSpinBox#ctrlSpinBox {"
        "  background: @overlayMut; border: 1px solid rgba(255,255,255,0.10);"
        "  border-radius: @radius2; color: @textSecond; font-family: @font; font-size: @fontSm;"
        "  padding: 1px 4px;"
        "}"
        "QDoubleSpinBox#ctrlSpinBox:focus, QSpinBox#ctrlSpinBox:focus {"
        "  border-color: rgba(@accentRgb,0.5);"
        "}"
        "QDoubleSpinBox#ctrlSpinBox:disabled, QSpinBox#ctrlSpinBox:disabled {"
        "  background: rgba(255,255,255,0.02); border-color: @overlayEl;"
        "  color: @textDisabled;"
        "}"
        "QFrame#ctrlDivider { background: @border; }"

        // Control panel tab switcher
        "QWidget#ctrlTabBar { background: @bgEl; border-bottom: 1px solid @border; }"
        "QToolButton#ctrlTab {"
        "  background: transparent; border: none; border-radius: @radius2;"
        "  color: @textSubtle; font-family: @font; font-size: @fontXs; font-weight: 600;"
        "  padding: 3px 4px; min-height: 20px;"
        "}"
        "QToolButton#ctrlTab:hover   { background: @overlayEl; color: @textSecond; }"
        "QToolButton#ctrlTab:checked { background: rgba(@accentRgb,0.15); color: @accentSoft; }"
        "QToolButton#ctrlTab:disabled { color: @textDisabled; }"

        // ---------------------------------------------------------------------
        // Shared side-panel chrome — SidePanelShell + PanelTabBar
        // The left File Explorer dock and the right Properties panel (both panes)
        // share these rules so the two sides can never visually drift.
        // ---------------------------------------------------------------------
        "QFrame#sidePanelShell { background: @bgPanel; border: none; }"

        "QWidget#panelTabBar { background: @bgEl; border-bottom: 1px solid @border; }"
        "QToolButton#panelTab {"
        "  background: transparent; border: none; border-bottom: 2px solid transparent;"
        "  color: @textSubtle; font-family: @font; font-size: @fontSm; font-weight: 500;"
        "  padding: 0 4px; min-height: 36px;"
        "}"
        "QToolButton#panelTab:hover    { color: @textSecond; background: @overlayEl; }"
        "QToolButton#panelTab:checked  { color: @textPrimary; border-bottom: 2px solid @accent; }"
        "QToolButton#panelTab:disabled { color: @textDisabled; border-bottom-color: transparent; }"
        "QFrame#panelTabSep { background: @border; min-width: 1px; max-width: 1px; border: none; }"

        "QSplitter#propsSplitter { background: @bg; }"
        "QSplitter#propsSplitter::handle { background: @border; }"

        "QListWidget#propsHistoryList {"
        "  background: transparent; border: none;"
        "  color: @textSecond; font-family: @font; font-size: @fontSm;"
        "}"
        "QListWidget#propsHistoryList::item { padding: 5px 8px; border-radius: @radius2; }"
        "QListWidget#propsHistoryList::item:hover { background: @overlayEl; }"
        "QListWidget#propsHistoryList::item:selected {"
        "  background: rgba(@accentRgb,0.12); color: @textPrimary;"
        "}"

        "QPushButton#ctrlApplyBtn {"
        "  background: rgba(@accentRgb,0.12); border: 1px solid rgba(@accentRgb,0.3);"
        "  border-radius: @radius2; color: @accentSoft; font-family: @font; font-size: @fontSm;"
        "  padding: 4px 12px; min-height: 22px;"
        "}"
        "QPushButton#ctrlApplyBtn:hover    { background: rgba(@accentRgb,0.20); border-color: rgba(@accentRgb,0.45); }"
        "QPushButton#ctrlApplyBtn:pressed  { background: rgba(@accentRgb,0.08); }"
        "QPushButton#ctrlApplyBtn:disabled { background: rgba(255,255,255,0.03); border-color: @border; color: @textDisabled; }"

        "QPushButton#ctrlApplyBtnSecondary {"
        "  background: @overlayMut; border: 1px solid rgba(255,255,255,0.12);"
        "  border-radius: @radius2; color: @textSubtle; font-family: @font; font-size: @fontSm;"
        "  padding: 4px 12px; min-height: 22px;"
        "}"
        "QPushButton#ctrlApplyBtnSecondary:hover    { background: rgba(255,255,255,0.09); color: @textSecond; }"
        "QPushButton#ctrlApplyBtnSecondary:pressed  { background: rgba(255,255,255,0.03); }"
        "QPushButton#ctrlApplyBtnSecondary:disabled { background: rgba(255,255,255,0.02); border-color: @border; color: @textDisabled; }"

        "QLabel#ctrlEmptyHint {"
        "  color: @textSubtle; font-family: @font; font-size: @fontSm;"
        "  padding: 16px 8px;"
        "}"

        // ---------------------------------------------------------------------
        // Bottom dock panel chrome
        // ---------------------------------------------------------------------

        "QWidget#bottomDockPanel { background: @bgPanel; border-top: 1px solid @border; }"

        "QWidget#bottomPanelHeader { background: @bg; border-bottom: 1px solid @border; }"

        "QToolButton#panelTabBtn {"
        "  color: @textMuted; background: transparent; border: none;"
        "  border-bottom: 2px solid transparent;"
        "  padding: 4px 10px 3px 10px; font-size: @fontBase;"
        "}"
        "QToolButton#panelTabBtn:checked {"
        "  color: @textPrimary; border-bottom: 2px solid @accent;"
        "}"
        "QToolButton#panelTabBtn:hover:!checked { color: @iconStroke; }"

        "QLabel#panelBadge {"
        "  background: @warning; color: white; border-radius: 7px;"
        "  padding: 0px 5px; font-size: @fontXs; font-weight: bold;"
        "}"
        "QLabel#panelBadge[severity=\"error\"] { background: @danger; }"

        "QToolButton#panelCollapseBtn, QToolButton#panelCloseBtn {"
        "  background: transparent; border: none; border-radius: 4px;"
        "}"
        "QToolButton#panelCollapseBtn:hover, QToolButton#panelCloseBtn:hover {"
        "  background: rgba(255,255,255,0.08);"
        "}"

        "QWidget#panelResizeHandle { background: @overlayMut; }"
        "QWidget#panelResizeHandle:hover { background: rgba(@accentRgb,0.30); }"
        "QWidget#panelResizeHandle[dragging=\"true\"] { background: rgba(@accentRgb,0.55); }"

        // Views section (left panel) — per-viewer display settings
        "QWidget#viewsPanel { background: transparent; }"
        "QLabel#viewsRowLabel { color: @textSubtle; font-family: @font; font-size: @fontSm; }"
        "QComboBox#viewsCombo {"
        "  background: @overlayMut; border: 1px solid @overlayHov; border-radius: @radius2;"
        "  color: @textSecond; font-family: @font; font-size: @fontSm;"
        "  padding: 0 18px 0 7px;"
        "}"
        "QComboBox#viewsCombo:hover    { background: @overlayEl; border-color: @overlayActive; }"
        "QComboBox#viewsCombo:disabled { color: @textDisabled; }"
        "QComboBox#viewsCombo::drop-down { border: none; width: 16px; subcontrol-origin: padding; subcontrol-position: right center; }"
        "QComboBox#viewsCombo::down-arrow { image: url(:/icons/spin_down.svg); width: 8px; height: 6px; }"
        "QComboBox#viewsCombo QAbstractItemView {"
        "  background: @bgCard; border: 1px solid @borderMenu;"
        "  color: @textPrimary; selection-background-color: rgba(@accentRgb,0.25);"
        "  outline: none; padding: 2px 0; font-family: @font; font-size: @fontSm;"
        "}"
        "QLabel#viewsHint { color: @textDim; font-family: @font; font-size: @fontXs; }"
        "QLabel#viewsDrapeName { color: @textDim; font-family: @font; font-size: @fontSm; }"
        "QLabel#viewsDrapeName[drapeSet=\"true\"] { color: @textSecond; }"
        "QToolButton#viewsDrapeBtn {"
        "  background: @overlayMut; border: 1px solid @overlayHov; border-radius: @radius2;"
        "  color: @textSubtle; font-size: @fontSm; padding: 1px 6px;"
        "}"
        "QToolButton#viewsDrapeBtn:hover   { background: @overlayEl; color: @textPrimary; }"
        "QToolButton#viewsDrapeBtn:pressed { background: @overlayMut; }"

        // Problems / Jobs lists inside the dock
        "QListWidget#panelProbList, QListWidget#panelJobList {"
        "  background: @bgPanel; border: none; outline: none;"
        "  font-size: @fontBase; color: @textPrimary;"
        "}"
        "QListWidget#panelProbList::item, QListWidget#panelJobList::item {"
        "  padding: 4px 8px; border-radius: 0px; border-bottom: 1px solid @border;"
        "}"
        "QListWidget#panelProbList::item:selected, QListWidget#panelJobList::item:selected {"
        "  background: rgba(@accentRgb,0.18);"
        "}"
        "QListWidget#panelProbList::item:hover:!selected,"
        "QListWidget#panelJobList::item:hover:!selected {"
        "  background: rgba(255,255,255,0.04);"
        "}"

        // Output log text area
        "QPlainTextEdit#panelOutput {"
        "  background: @bgPanel; color: @iconStroke; border: none;"
        "  font-family: 'Consolas', 'Cascadia Code', monospace; font-size: @fontSm;"
        "  selection-background-color: rgba(@accentRgb,0.3);"
        "}"

        // ---------------------------------------------------------------------
        // Terminal widget
        // ---------------------------------------------------------------------

        "QWidget#terminalWidget { background: @bgPanel; }"

        "QWidget#terminalHeader { background: @bg; border-bottom: 1px solid @border; }"

        "QLabel#terminalCwdLabel {"
        "  color: @textMuted; font-family: Consolas, 'Cascadia Code', monospace;"
        "  font-size: @fontSm; background: transparent; border: none;"
        "}"

        "QToolButton#terminalKillBtn {"
        "  color: @dangerBright; background: transparent; border: none; font-size: @fontSm;"
        "}"
        "QToolButton#terminalKillBtn:hover    { color: @dangerBrightHov; }"
        "QToolButton#terminalKillBtn:disabled { color: @textMuted; }"

        "QToolButton#terminalClearBtn {"
        "  color: @textMuted; background: transparent; border: none; font-size: @fontMd;"
        "}"
        "QToolButton#terminalClearBtn:hover { color: @textPrimary; }"

        "QPlainTextEdit#terminalOutput {"
        "  background: @bgPanel; color: @iconStroke; border: none;"
        "  font-family: 'Consolas', 'Cascadia Code', monospace; font-size: @fontXs;"
        "  selection-background-color: rgba(@accentRgb,0.3);"
        "}"

        "QWidget#terminalInputRow { background: @bg; border-top: 1px solid @border; }"

        "QLabel#terminalPromptArrow {"
        "  color: @accent; font-family: Consolas, monospace;"
        "  font-weight: bold; font-size: @fontMd; background: transparent; border: none;"
        "}"

        "QLineEdit#terminalInput {"
        "  background: transparent; color: @textPrimary; border: none;"
        "  border-radius: 0; padding: 0; font-size: @fontSm;"
        "}"
        "QLineEdit#terminalInput:disabled { color: @textMuted; }"

    );
}

} // namespace dolphin::ui::detail
