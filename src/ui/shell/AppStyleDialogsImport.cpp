#include "ui/shell/AppStylePrivate.h"

namespace dolphin::ui::detail {

QString qssDialogImport()
{
    return QString(

        // ---------------------------------------------------------------------
        // Import Dialog
        // ---------------------------------------------------------------------

        "ImportDialog { background: @bg; }"

        "#dlgHeader   { background: @bgEl; }"
        "#dlgTitle    { color: @textPrimary; font-family: @font; font-size: @fontLg; font-weight: 600; }"
        "#dlgSubtitle { color: @textMuted; font-family: @font; font-size: @fontSm; }"
        "#dlgDivider  { background: @border; }"
        "#dlgBody     { background: @bg; }"

        "#dlgSectionLabel {"
        "  color: @textMuted; font-family: @font; font-size: @fontXs;"
        "  font-weight: 600; letter-spacing: 0.2px;"
        "}"
        "#dlgSection {"
        "  background: @bgEl; border: 1px solid @border; border-radius: 8px;"
        "}"

        // -- Shared semantic labels (import wizard, metadata windows, etc.) -----
        "QLabel#dlgLabelMeta {"
        "  color: @textMuted; font-family: @font; font-size: @fontSm;"
        "}"
        "QLabel#dlgLabelDanger  { color: @danger;  font-family: @font; font-size: @fontSm; }"
        "QLabel#dlgLabelCaution { color: @caution; font-family: @font; font-size: @fontSm; }"
        "QLabel#dlgLabelMono {"
        "  font-family: monospace; font-size: @fontSm; color: @textSoft;"
        "}"
        "QLabel#dlgLabelMonoSmall {"
        "  font-family: monospace; font-size: @fontXs; color: @textSubtle;"
        "}"
        // Per-file status badge in the import wizard — state drives colour.
        "QLabel#importFileStatus { color: @textMuted; font-family: @font; font-size: @fontSm; }"
        "QLabel#importFileStatus[state=\"ok\"]      { color: @success; }"
        "QLabel#importFileStatus[state=\"caution\"] { color: @caution; }"
        "QLabel#importFileStatus[state=\"error\"]   { color: @danger;  }"

        // ---------------------------------------------------------------------
        // Metadata windows (SBP + SSS)
        // ---------------------------------------------------------------------

        "QWidget#metaToolbar {"
        "  background: @bgEl; border-bottom: 1px solid @border;"
        "}"
        "QWidget#metaToolbar QToolButton {"
        "  background: transparent; border: none; border-radius: @radius2;"
        "  padding: @padSm; color: @textSecond;"
        "}"
        "QWidget#metaToolbar QToolButton:hover    { background: @overlayHov; }"
        "QWidget#metaToolbar QToolButton:pressed  { background: rgba(255,255,255,0.12); }"
        "QWidget#metaToolbar QToolButton::menu-indicator { image: none; }"

        "QFrame#metaSep { color: @border; }"

        "QLabel#metaLoadStatus { color: @textMuted; font-family: @font; font-size: @fontSm; }"

        "QLabel#metaSelStatus {"
        "  background: @bg; color: @textSubtle; padding: 0 6px; font-size: @fontSm;"
        "}"

        "QToolButton#metaUndockBtn {"
        "  background: transparent; border: none; color: @textSubtle; font-size: @fontXs;"
        "}"
        "QToolButton#metaUndockBtn:hover {"
        "  color: @textSoft; background: @overlayHov; border-radius: @radius1;"
        "}"

        "#dlgRadio            { color: @textSecond; }"
        "#dlgRadioLabel       { color: @textSecond; font-family: @font; font-size: @fontBase; }"
        "#dlgRadioLabelDim    { color: @textDisabled; font-family: @font; font-size: @fontBase; }"
        "#dlgProjectName      { color: @accent; font-family: @font; font-size: @fontBase; font-weight: 600; }"
        "#dlgNewProjForm      { background: transparent; }"
        "#dlgFieldLabel       { color: @textSubtle; font-family: @font; font-size: @fontSm; }"
        "#dlgFolderPath       { color: @textMuted; font-family: @font; font-size: @fontXs; }"

        // Hint/helper text inside dialogs — italic, muted
        "QLabel#dlgHint {"
        "  color: @textSubtle; font-family: @font; font-size: @fontSm; font-style: italic;"
        "  padding: 2px 2px 4px 2px;"
        "}"

        "#dlgLineEdit {"
        "  background: @bgCard; border: 1px solid @borderMenu; border-radius: 5px;"
        "  color: @textPrimary; font-family: @font; font-size: @fontBase;"
        "  padding: 3px 8px; selection-background-color: rgba(@accentRgb,0.4);"
        "}"
        "#dlgLineEdit:focus { border-color: @accent; }"

        "#dlgFileTable {"
        "  background: @bgPanel; border: 1px solid @border; border-radius: @radius3;"
        "  color: @textSecond; font-family: @font; font-size: @fontSm;"
        "  gridline-color: @bg;"
        "  selection-background-color: rgba(@accentRgb,0.18);"
        "}"
        "#dlgTableHdr {"
        "  background: @bgEl; color: @textMuted; font-family: @font;"
        "  font-size: @fontXxs; font-weight: 700; letter-spacing: 0.5px;"
        "  border: none; border-bottom: 1px solid @border;"
        "}"
        "#dlgFileTable QHeaderView::section {"
        "  background: @bgEl; color: @textMuted; font-family: @font;"
        "  font-size: @fontXxs; font-weight: 700; letter-spacing: 0.5px;"
        "  padding: 4px 8px; border: none; border-right: 1px solid @border;"
        "}"

        "#dlgActionCombo {"
        "  background: @bgCard; border: 1px solid @borderMenu; border-radius: @radius2;"
        "  color: @textSecond; font-family: @font; font-size: @fontSm;"
        "  padding: 2px 20px 2px 6px; text-align: left;"
        "}"
        "#dlgActionCombo::drop-down { border: none; width: 20px; subcontrol-origin: padding; subcontrol-position: right center; }"
        "#dlgActionCombo QAbstractItemView {"
        "  background: @bgCard; border: 1px solid @borderMenu;"
        "  color: @textPrimary; selection-background-color: @accent;"
        "}"

        "#dlgFooter  { background: @bgEl; border-top: 1px solid @border; }"
        "#dlgSummary { color: @textMuted; font-family: @font; font-size: @fontSm; }"

        "#dlgProgressStatus {"
        "  color: @textSubtle; font-family: @font; font-size: @fontMd;"
        "}"

        "#dlgProgressBar {"
        "  border: none; border-radius: 2px;"
        "  background: @overlayHov; max-height: 4px; min-height: 4px;"
        "}"
        "#dlgProgressBar::chunk {"
        "  border-radius: 2px;"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 @accent, stop:1 @accentSoft);"
        "}"

        "#dlgBtnPrimary {"
        "  background: @accent; border: none; border-radius: @radius3;"
        "  color: @white; font-family: @font; font-size: @fontBase; font-weight: 600; padding: 0 20px;"
        "}"
        "#dlgBtnPrimary:hover    { background: @accentHover; }"
        "#dlgBtnPrimary:pressed  { background: @accentPress; }"
        "#dlgBtnPrimary:disabled { background: @bgCard; color: @textDisabled; }"

        "#dlgBtnSecondary {"
        "  background: @overlayEl; border: 1px solid @borderMenu; border-radius: @radius3;"
        "  color: @textSecond; font-family: @font; font-size: @fontBase; padding: 0 16px;"
        "}"
        "#dlgBtnSecondary:hover    { background: @overlayHov; }"
        "#dlgBtnSecondary:pressed  { background: rgba(255,255,255,0.03); }"
        "#dlgBtnSecondary:disabled { background: rgba(255,255,255,0.03); border-color: @border; color: @textDisabled; }"

        "#dlgBtnBrowse {"
        "  background: @overlayEl; border: 1px solid @borderMenu; border-radius: @radius2;"
        "  color: @textSubtle; font-family: @font; font-size: @fontXs; padding: 2px 10px;"
        "}"
        "#dlgBtnBrowse:hover    { background: @overlayHov; }"
        "#dlgBtnBrowse:pressed  { background: rgba(255,255,255,0.03); }"
        "#dlgBtnBrowse:disabled { background: transparent; border-color: @border; color: @textDisabled; }"

        "#dlgBtnAdd {"
        "  background: transparent; border: 1px solid @borderMenu; border-radius: @radius2;"
        "  color: @accent; font-family: @font; font-size: @fontXs; font-weight: 600; padding: 2px 10px;"
        "}"
        "#dlgBtnAdd:hover    { background: rgba(@accentRgb,0.10); }"
        "#dlgBtnAdd:pressed  { background: rgba(@accentRgb,0.05); }"
        "#dlgBtnAdd:disabled { border-color: @border; color: @textDisabled; }"

        // ---------------------------------------------------------------------
        // Import panel action buttons
        // ---------------------------------------------------------------------

        "QPushButton#importActionBtn {"
        "  background: @overlayMut; border: 1px solid @borderMenu; border-radius: @radius2;"
        "  color: @textSecond; font-family: @font; font-size: @fontBase;"
        "  padding: 6px 12px; text-align: left; min-height: 48px;"
        "}"
        "QPushButton#importActionBtn:hover    { background: @overlayHov; border-color: rgba(@accentRgb,0.4); }"
        "QPushButton#importActionBtn:pressed  { background: rgba(@accentRgb,0.15); }"
        "QPushButton#importActionBtn:disabled { background: rgba(255,255,255,0.02); border-color: @border; color: @textDisabled; }"

        // Sensor-type selector cards in ImportSetupDialog (QFrame used, not QPushButton)
        "QFrame#importActionBtn {"
        "  background: @overlayMut; border: 1px solid @borderMenu; border-radius: @radius2;"
        "}"
        "QFrame#importActionBtn:hover { background: @overlayHov; border-color: rgba(@accentRgb,0.4); }"
        "QFrame#importActionBtn[selected=\"true\"] {"
        "  background: rgba(@accentRgb,0.10); border-color: rgba(@accentRgb,0.45);"
        "}"

        "QPushButton#importPrimaryBtn {"
        "  background: rgba(@accentRgb,0.10); border: 1px solid rgba(@accentRgb,0.35); border-radius: @radius2;"
        "  color: @accentSoft; font-family: @font; font-size: @fontBase;"
        "  padding: 6px 12px; text-align: left; min-height: 48px;"
        "}"
        "QPushButton#importPrimaryBtn:hover    { background: rgba(@accentRgb,0.18); border-color: rgba(@accentRgb,0.5); }"
        "QPushButton#importPrimaryBtn:pressed  { background: rgba(@accentRgb,0.08); }"
        "QPushButton#importPrimaryBtn:disabled { background: rgba(@accentRgb,0.04); border-color: rgba(@accentRgb,0.15); color: @textDisabled; }"

    );
}

} // namespace dolphin::ui::detail
