#include "ui/shell/AppStylePrivate.h"

namespace dolphin::ui::detail {

QString qssAcousticViews()
{
    return QString(

        // ---------------------------------------------------------------------
        // Acoustic Viewer Chrome  (WaterfallWindow + SubBottomWindow)
        // ---------------------------------------------------------------------

        "WaterfallWindow  { background: @bg; font-family: @font; }"
        "SubBottomWindow  { background: @bg; font-family: @font; }"

        "#av_toolbar {"
        "  background: @bgEl; border-bottom: 1px solid @border;"
        "  spacing: 1px; padding: 0 10px;"
        "}"
        "#avQuickBtn {"
        "  background: transparent; border: none;"
        "  border-radius: @radius3; padding: 6px 7px; min-width: 30px;"
        "}"
        "#avQuickBtn:hover   { background: @overlayHov; }"
        "#avQuickBtn:pressed { background: rgba(255,255,255,0.04); }"
        "#avQuickBtn:checked { background: rgba(@accentRgb,0.22); }"
        "#avQuickBtn:disabled { background: transparent; }"

        "QLineEdit#avCommandBar {"
        "  background: @overlayEl; border: 1px solid @overlayHov;"
        "  border-radius: @radius3; color: @textSecond;"
        "  font-family: @font; font-size: @fontBase; padding: 0 12px;"
        "  selection-background-color: rgba(@accentRgb,0.4);"
        "}"
        "QLineEdit#avCommandBar:hover {"
        "  background: rgba(255,255,255,0.09);"
        "  border-color: rgba(255,255,255,0.14);"
        "}"
        "QLineEdit#avCommandBar:focus {"
        "  border-color: rgba(@accentRgb,0.5); background: rgba(255,255,255,0.09);"
        "}"

        "#av_inspector, #av_analysis { background: @bgPanel; }"
        "#av_divider                 { background: @border; }"

        "QScrollArea#av_panel_scroll { background: transparent; border: none; }"
        "QScrollArea#av_panel_scroll > QWidget { background: @bgPanel; border: none; }"
        "QScrollArea#av_panel_scroll > QWidget > QWidget { background: @bgPanel; }"

        // Panel-internal scrollbars
        "QScrollArea#av_panel_scroll QScrollBar:vertical {"
        "  background: transparent; width: 5px; border: none; margin: 0;"
        "}"
        "QScrollArea#av_panel_scroll QScrollBar::handle:vertical {"
        "  background: rgba(255,255,255,0.18); border-radius: 2px; min-height: 20px;"
        "}"
        "QScrollArea#av_panel_scroll QScrollBar::handle:vertical:hover {"
        "  background: rgba(255,255,255,0.32);"
        "}"
        "QScrollArea#av_panel_scroll QScrollBar::add-line:vertical,"
        "QScrollArea#av_panel_scroll QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollArea#av_panel_scroll QScrollBar::add-page:vertical,"
        "QScrollArea#av_panel_scroll QScrollBar::sub-page:vertical { background: none; }"

        "QScrollBar#wf_vscroll:vertical { background: transparent; width: 8px; border: none; }"
        "QScrollBar#wf_vscroll::handle:vertical {"
        "  background: rgba(255,255,255,0.22); border-radius: @radius2; min-height: 32px;"
        "}"
        "QScrollBar#wf_vscroll::handle:vertical:hover { background: rgba(255,255,255,0.35); }"
        "QScrollBar#wf_vscroll::add-line:vertical,"
        "QScrollBar#wf_vscroll::sub-line:vertical { height: 0; }"

        // Collapsible section headers
        "QPushButton#avCollapseHdr {"
        "  background: @bgEl; border: none; border-top: 1px solid @border;"
        "  color: @textMuted; font-family: @font; font-size: @fontSm; font-weight: 500;"
        "  letter-spacing: 0.2px; text-align: left; padding: 0 0 0 10px;"
        "}"
        "QPushButton#avCollapseHdr:hover   { color: @textSecond; background: rgba(@accentRgb,0.06); }"
        "QPushButton#avCollapseHdr:pressed { background: @bgEl; }"
        "QPushButton#avCollapseHdr[wfDirty=\"true\"]       { color: @accent; }"
        "QPushButton#avCollapseHdr[wfDirty=\"true\"]:hover { color: @accent; }"
        "#avCollapseBody { background: @bgPanel; }"

        "#wfSectionHdr {"
        "  background: @bgEl;"
        "  border-top: 1px solid @border; border-bottom: 1px solid @border;"
        "  min-height: 28px; max-height: 28px;"
        "}"
        "#wfSectionTitle {"
        "  color: @textMuted; font-family: @font; font-size: @fontXs;"
        "  font-weight: 600; letter-spacing: 0.3px;"
        "}"

        "#avMetaKey     { color: @textMuted;   font-family: @font; font-size: @fontSm; }"
        "#avMetaVal     { color: @iconStroke;  font-family: @font; font-size: @fontSm; font-weight: 500; }"
        "#avMetaValWide { color: @iconStroke;  font-family: @font; font-size: @fontSm; font-weight: 500; padding: 1px 0 3px 0; }"

        "#wfParamLabel { color: @textSubtle; font-family: @font; font-size: @fontSm; }"

        "QLineEdit#wfValueEdit {"
        "  background: rgba(@accentRgb,0.14); border: 1px solid rgba(@accentRgb,0.65);"
        "  border-radius: 5px; color: @textSecond;"
        "  font-family: @font; font-size: @fontSm; font-weight: 600;"
        "  padding: 0 8px 0 4px;"
        "  selection-background-color: rgba(@accentRgb,0.45);"
        "}"

        // Legacy aliases
        "#wfSliderWidget { background: transparent; }"
        "#wfSliderLabel  { color: @textMuted; font-family: @font; font-size: @fontSm; }"

        "QComboBox#avPaletteCombo {"
        "  background: @overlayEl; border: 1px solid @overlayHov;"
        "  border-radius: 5px; color: @textSecond;"
        "  font-family: @font; font-size: @fontSm;"
        "  padding: 2px 24px 2px 8px; text-align: left;"
        "}"
        "QComboBox#avPaletteCombo::drop-down {"
        "  border: none; width: 20px; subcontrol-origin: padding; subcontrol-position: right center;"
        "}"
        "QComboBox#avPaletteCombo::down-arrow { width: 8px; height: 8px; }"
        "QComboBox#avPaletteCombo QAbstractItemView {"
        "  background: @bgCard; border: 1px solid @borderMenu; border-radius: 8px;"
        "  color: @textPrimary; selection-background-color: rgba(@accentRgb,0.3);"
        "  padding: 4px 0; font-size: @fontSm; text-align: left;"
        "}"

        // Phase 2 placeholder text buttons (Gain Brush / Threshold Brush) — barely visible
        "#wfBrushBtn {"
        "  background: transparent; border: none;"
        "  border-radius: @radius3; padding: 5px 8px;"
        "  color: @textDisabled; font-family: @font; font-size: @fontSm; font-weight: 600;"
        "}"
        "#wfBrushBtn:disabled { color: @textDisabled; }"

        // Seabed manual tool icon buttons (Pen / Insert / Eraser)
        "#wfSeabedTool {"
        "  background: transparent; border: 1px solid transparent;"
        "  border-radius: @radius3; padding: @padXs;"
        "  color: @textSubtle; font-family: @font; font-size: 17px;"
        "}"
        "#wfSeabedTool:hover    { background: rgba(255,255,255,0.07); color: @textSecond; }"
        "#wfSeabedTool:checked  { background: rgba(@accentRgb,0.18); color: @accent; border-color: rgba(@accentRgb,0.35); }"
        "#wfSeabedTool:pressed  { background: rgba(@accentRgb,0.10); }"
        "#wfSeabedTool:disabled { color: @textDisabled; }"

        // Sub-section labels inside collapsible panels (e.g. AUTO / MANUAL)
        "#wfSubSectionLabel {"
        "  color: @textSubtle; font-family: @font; font-size: @fontXs;"
        "  font-weight: 600; letter-spacing: 0.2px;"
        "}"

        // Outline toggle button — unchecked: ghost border; checked: solid accent fill
        "QToolButton#avToggleBtn {"
        "  background: transparent; border: 1px solid rgba(255,255,255,0.12);"
        "  border-radius: 5px; padding: 3px 8px;"
        "  color: @textMuted; font-family: @font; font-size: @fontSm; font-weight: 600;"
        "}"
        "QToolButton#avToggleBtn:hover    { border-color: rgba(255,255,255,0.22); color: @iconStroke; }"
        "QToolButton#avToggleBtn:checked  { background: rgba(@accentRgb,0.15); border-color: rgba(@accentRgb,0.40); color: @accent; }"
        "QToolButton#avToggleBtn:pressed  { background: rgba(@accentRgb,0.08); }"
        "QToolButton#avToggleBtn:disabled { color: @textDisabled; border-color: @overlayEl; }"

        // Generic tool buttons in analysis panel (contact pick, clear all)
        "QToolButton#wfToolBtn {"
        "  background: transparent; border: 1px solid rgba(255,255,255,0.10);"
        "  border-radius: 5px; padding: 3px 8px;"
        "  color: @textSubtle; font-family: @font; font-size: @fontSm;"
        "}"
        "QToolButton#wfToolBtn:hover    { border-color: rgba(255,255,255,0.20); color: @iconStroke; }"
        "QToolButton#wfToolBtn:checked  { background: rgba(@accentRgb,0.15); border-color: rgba(@accentRgb,0.40); color: @accent; }"
        "QToolButton#wfToolBtn:pressed  { background: rgba(@accentRgb,0.08); }"
        "QToolButton#wfToolBtn:disabled { color: @textDisabled; border-color: @overlayMut; }"

        // Checkboxes inside the analysis panel
        "QCheckBox#wfCheckBox {"
        "  color: @iconStroke; font-family: @font; font-size: @fontSm; spacing: 6px;"
        "}"
        "QCheckBox#wfCheckBox::indicator {"
        "  width: 14px; height: 14px; border-radius: @radius1;"
        "  border: 1px solid rgba(255,255,255,0.15); background: @overlayMut;"
        "}"
        "QCheckBox#wfCheckBox::indicator:hover    { border-color: rgba(255,255,255,0.30); }"
        "QCheckBox#wfCheckBox::indicator:checked  { background: @accent; border-color: @accent; }"
        "QCheckBox#wfCheckBox::indicator:disabled { background: rgba(255,255,255,0.03); border-color: @overlayHov; }"
        "QCheckBox#wfCheckBox:disabled { color: @textDisabled; }"

        // Generic combo box used inside waterfall panels
        "QComboBox#wfCombo {"
        "  background: @overlayEl; border: 1px solid @overlayHov;"
        "  border-radius: 5px; color: @textSecond;"
        "  font-family: @font; font-size: @fontSm;"
        "  padding: 2px 24px 2px 8px; text-align: left;"
        "}"
        "QComboBox#wfCombo:focus { border-color: rgba(@accentRgb,0.5); }"
        "QComboBox#wfCombo::drop-down { border: none; width: 20px; subcontrol-origin: padding; subcontrol-position: right center; }"
        "QComboBox#wfCombo QAbstractItemView {"
        "  background: @bgCard; border: 1px solid @borderMenu; border-radius: 8px;"
        "  color: @textPrimary; selection-background-color: rgba(@accentRgb,0.3);"
        "  padding: 4px 0; font-size: @fontSm; text-align: left;"
        "}"

        // Frequency band selector in the toolbar — same semi-transparent treatment as wfCombo
        "QComboBox#wfFreqSelector {"
        "  background: @overlayEl; border: 1px solid @overlayHov;"
        "  border-radius: 5px; color: @textSecond;"
        "  font-family: @font; font-size: @fontSm;"
        "  padding: 2px 24px 2px 8px; text-align: left;"
        "}"
        "QComboBox#wfFreqSelector:focus { border-color: rgba(@accentRgb,0.5); }"
        "QComboBox#wfFreqSelector::drop-down { border: none; width: 20px; subcontrol-origin: padding; subcontrol-position: right center; }"
        "QComboBox#wfFreqSelector QAbstractItemView {"
        "  background: @bgCard; border: 1px solid @borderMenu; border-radius: 8px;"
        "  color: @textPrimary; selection-background-color: rgba(@accentRgb,0.3);"
        "  padding: 4px 0; font-size: @fontSm; text-align: left;"
        "}"

        // "Set CRS" button shown when CRS is unconfirmed
        "QPushButton#setCrsButton {"
        "  background: rgba(@cautionRgb,0.10); border: 1px solid rgba(@cautionRgb,0.30);"
        "  border-radius: 5px; color: @caution;"
        "  font-family: @font; font-size: @fontSm; font-weight: 600;"
        "  padding: 3px 10px; margin: 4px 12px 2px 12px;"
        "}"
        "QPushButton#setCrsButton:hover    { background: rgba(@cautionRgb,0.18); border-color: rgba(@cautionRgb,0.50); }"
        "QPushButton#setCrsButton:pressed  { background: rgba(@cautionRgb,0.08); }"
        "QPushButton#setCrsButton:disabled { background: rgba(255,255,255,0.04); border-color: @border; color: @textDisabled; }"

        // Nav buttons (Prev Line / Next Line)
        "#avNavBtn {"
        "  background: @accent; border: none;"
        "  border-radius: @radius3; padding: 5px 10px;"
        "  color: @white; font-family: @font; font-size: @fontBase; font-weight: 600;"
        "}"
        "#avNavBtn:hover    { background: @accentHover; }"
        "#avNavBtn:pressed  { background: @accentPress; }"
        "#avNavBtn:disabled { background: rgba(@accentRgb,0.25); color: rgba(255,255,255,0.4); border: none; }"

        // Apply button — solid accent
        "#wfApplyBtn {"
        "  background: @accent; border: none;"
        "  border-radius: @radius3; padding: 6px 12px;"
        "  color: @white; font-family: @font; font-size: @fontBase; font-weight: 600;"
        "}"
        "#wfApplyBtn:hover    { background: @accentHover; }"
        "#wfApplyBtn:pressed  { background: @accentPress; }"
        "#wfApplyBtn:disabled {"
        "  background: @overlayMut; border: 1px solid @border;"
        "  color: @textDisabled;"
        "}"

        "QFrame#avHRule { background: @border; max-height: 1px; border: none; }"

        // Processing tool checkboxes (Phase 2 — shown disabled in inspector panel)
        "QCheckBox#wfProcessingCheck {"
        "  color: @textMuted; font-family: @font; font-size: @fontSm; spacing: 6px;"
        "}"
        "QCheckBox#wfProcessingCheck::indicator {"
        "  width: 13px; height: 13px; border-radius: @radius1;"
        "  border: 1px solid rgba(255,255,255,0.10); background: rgba(255,255,255,0.03);"
        "}"
        "QCheckBox#wfProcessingCheck:disabled { color: @textDisabled; }"

        // Bottom progress bar — thin accent stripe, no text
        "QProgressBar#wfProgressBar {"
        "  background: rgba(255,255,255,0.07); border: none; border-radius: @radius1;"
        "}"
        "QProgressBar#wfProgressBar::chunk {"
        "  background: @accent; border-radius: @radius1;"
        "}"

        "#av_bottombar  { background: @bgEl; border-top: 1px solid @border; }"
        "#wfStatus      { color: @textMuted;  font-family: @font; font-size: @fontSm; }"
        "#wfStatusLine  { color: @textMuted;  font-family: @font; font-size: @fontSm; font-weight: 600; }"
        "#wfStatusCoord {"
        "  color: @textSoft; font-family: @font; font-size: @fontSm;"
        "  padding: 0 8px 0 0;"
        "}"

    );
}

} // namespace dolphin::ui::detail
