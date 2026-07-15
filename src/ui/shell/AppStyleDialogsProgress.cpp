#include "ui/shell/AppStylePrivate.h"

namespace dolphin::ui::detail {

QString qssDialogProgress()
{
    return QString(

        // ---------------------------------------------------------------------
        // Import / Correction Dialogs (shared patterns)
        // ---------------------------------------------------------------------

        "SidescanCorrectionDialog { background: @bg; }"
        "QDialog#geodesyWin { background: @bg; }"

        // QCheckBox#dlgCheckBox — legacy alias; global QCheckBox style in AppStyleBase now covers all checkboxes.

        // ---------------------------------------------------------------------
        // Execution Progress Dialog
        // ---------------------------------------------------------------------

        "ExecutionProgressDialog { background: @bgEl; }"
        "QWidget#epdHeader { background: @bgEl; border-bottom: 1px solid @border; }"
        "QWidget#epdListBody { background: @bgEl; }"
        "QWidget#epdFooter  { background: @bgPanel; border-top: 1px solid @border; }"

        "ExecutionProgressDialog QLabel#titleLabel {"
        "  color: @textPrimary; font-family: @font; font-size: 15px; font-weight: 600;"
        "  background: transparent;"
        "}"
        "ExecutionProgressDialog QLabel#subtitleLabel {"
        "  color: @textSubtle; font-family: @font; font-size: @fontBase; background: transparent;"
        "}"

        "QProgressBar#overallBar {"
        "  border: none; border-radius: @radius1;"
        "  background: rgba(255,255,255,0.10); max-height: 6px; min-height: 6px;"
        "}"
        "QProgressBar#overallBar::chunk {"
        "  border-radius: @radius1;"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 @accent, stop:1 @accentSoft);"
        "}"

        "QFrame#fileCard {"
        "  background: transparent; border: none; border-bottom: 1px solid @border;"
        "}"
        "QFrame#fileCard:hover { background: @overlayHov; }"
        "QFrame#fileCard > QWidget { background: transparent; }"
        "QLabel#rowStatusIcon { background: transparent; }"

        "QLabel#formatBadge {"
        "  background: rgba(@accentRgb,0.12); border: 1px solid rgba(@accentRgb,0.30);"
        "  border-radius: @radius3; color: @accentSoft;"
        "  font-family: @font; font-size: @fontSm; font-weight: 700;"
        "  min-width: @badgeSize; max-width: @badgeSize; min-height: @badgeSize; max-height: @badgeSize;"
        "}"
        "QLabel#formatBadge[state=\"done\"] {"
        "  background: rgba(@successRgb,0.12); border-color: rgba(@successRgb,0.30);"
        "  color: @success;"
        "}"
        "QLabel#formatBadge[state=\"failed\"] {"
        "  background: rgba(@dangerRgb,0.12); border-color: rgba(@dangerRgb,0.30);"
        "  color: @danger;"
        "}"

        "ExecutionProgressDialog QLabel#fileName {"
        "  color: @textPrimary; font-family: @font; font-size: @fontBase; font-weight: 600;"
        "  background: transparent;"
        "}"
        "ExecutionProgressDialog QLabel#fileMeta {"
        "  color: @textMuted; font-family: @font; font-size: @fontSm; background: transparent;"
        "}"
        "ExecutionProgressDialog QLabel#fileStatus {"
        "  color: @textSubtle; font-family: @font; font-size: @fontSm; background: transparent;"
        "}"
        "ExecutionProgressDialog QLabel#fileResult {"
        "  color: @textSubtle; font-family: @font; font-size: @fontSm; background: transparent;"
        "}"
        "ExecutionProgressDialog QLabel#fileResult[state=\"done\"]   { color: @success; }"
        "ExecutionProgressDialog QLabel#fileResult[state=\"failed\"] { color: @danger; }"

        "QProgressBar#fileBar {"
        "  border: none; border-radius: 2px;"
        "  background: @overlayHov; max-height: 5px; min-height: 5px;"
        "}"
        "QProgressBar#fileBar::chunk {"
        "  border-radius: 2px;"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 @accent, stop:1 @accentSoft);"
        "}"

        "ExecutionProgressDialog QLabel#elapsedLabel {"
        "  color: @textMuted; font-family: @font; font-size: @fontSm; background: transparent;"
        "}"

        "QPushButton#bgBtn {"
        "  background: transparent; border: 1px solid @borderMenu; border-radius: @radius3;"
        "  color: @textSoft; font-family: @font; font-size: @fontBase;"
        "  padding: 5px 14px; min-width: 130px;"
        "}"
        "QPushButton#bgBtn:hover    { background: @overlayEl; color: @textPrimary; border-color: @borderMenu; }"
        "QPushButton#bgBtn:pressed  { background: rgba(255,255,255,0.03); }"
        "QPushButton#bgBtn:disabled { color: @textDisabled; border-color: @border; }"

        "QPushButton#closeBtn {"
        "  background: @accent; border: none; border-radius: @radius3;"
        "  color: @white; font-family: @font; font-size: @fontBase; font-weight: 600;"
        "  padding: 5px 20px; min-width: 80px;"
        "}"
        "QPushButton#closeBtn:hover    { background: @accentHover; }"
        "QPushButton#closeBtn:pressed  { background: @accentPress; }"
        "QPushButton#closeBtn:disabled { background: rgba(@accentRgb,0.30); color: rgba(255,255,255,0.35); }"

        // Command Palette Dialog
        "CommandPaletteDialog {"
        "  background: @bg;"
        "}"
        "#cpCard, #cpInputRow { background: transparent; }"
        "QLineEdit#cpInput {"
        "  background: transparent; border: none;"
        "  color: @textSecond; font-size: @fontMd; font-family: @font;"
        "  selection-background-color: rgba(@accentRgb,0.45);"
        "}"
        "QLineEdit#cpInput:focus { border: none; outline: none; }"
        "#cpSep { background: rgba(255,255,255,0.09); }"
        "#cpList { background: transparent; outline: 0; border: none; }"
        "#cpList::item { background: transparent; border: none; }"
        "#cpList::item:selected { background: transparent; border: none; }"

        // Note/hint labels inside dialogs (below form fields, muted italic)
        "QLabel#dlgNote {"
        "  color: @textSubtle; font-family: @font; font-size: @fontSm; font-style: italic;"
        "  padding: 4px 12px;"
        "}"

        // CRS required warning label in ImportDialog
        "QLabel#dlgCrsRequired {"
        "  color: @caution; font-family: @font; font-size: @fontSm;"
        "}"

        // ---------------------------------------------------------------------
        // New Project Dialog
        // ---------------------------------------------------------------------

        "QLabel#newProjCrsLabel {"
        "  color: @textSubtle; font-family: @font; font-size: @fontSm; font-style: italic;"
        "}"
        "QFrame#newProjPreviewBox {"
        "  background: @bgPanel; border: 1px solid @border; border-radius: @radius2;"
        "}"
        "QLabel#newProjPreviewPath {"
        "  color: @textSoft; font-family: monospace; font-size: @fontSm;"
        "}"

        // ---------------------------------------------------------------------
        // Processing Modal Dialog (frameless, drop-shadow overlay)
        // ---------------------------------------------------------------------

        "QFrame#dlgProcessingFrame {"
        "  background-color: @bgPanel; border: 1px solid @border; border-radius: 10px;"
        "}"
        "QWidget#procHeader {"
        "  background: transparent; border-bottom: 1px solid @border;"
        "}"
        "QLabel#procSpinner {"
        "  color: @accent; font-size: 20px; background: transparent; border: none;"
        "}"
        "QLabel#procTitle {"
        "  color: @textSecond; font-family: @font; font-size: @fontBase; font-weight: 600;"
        "  letter-spacing: 1px; background: transparent; border: none;"
        "}"
        "QPushButton#procCloseBtn {"
        "  color: @textMuted; font-size: @fontBase; background: transparent;"
        "  border: none; border-radius: @radius2;"
        "}"
        "QPushButton#procCloseBtn:hover { color: @white; background: @dangerBright; }"
        "QProgressBar#procBar { background: @bgPanel; border: none; }"
        "QProgressBar#procBar::chunk {"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 @accentPress, stop:1 @accent);"
        "}"
        "QLabel#procStatus {"
        "  color: @textPrimary; font-family: @font; font-size: @fontMd; font-weight: 600;"
        "  background: transparent;"
        "}"
        "QTextEdit#procLog {"
        "  background-color: @bg; color: @textSecond;"
        "  border: 1px solid @border; border-radius: @radius3; padding: 8px;"
        "}"
        "QTextEdit#procLog QScrollBar:vertical {"
        "  background: @bg; width: 5px; border: none; margin: 0;"
        "}"
        "QTextEdit#procLog QScrollBar::handle:vertical {"
        "  background: rgba(255,255,255,0.20); border-radius: 2px; min-height: 20px;"
        "}"
        "QTextEdit#procLog QScrollBar::handle:vertical:hover { background: @accent; }"
        "QTextEdit#procLog QScrollBar::add-line:vertical,"
        "QTextEdit#procLog QScrollBar::sub-line:vertical { height: 0; border: none; }"
        "QPushButton#procCancel {"
        "  color: @textMuted; font-family: @font; font-size: @fontBase; font-weight: 500;"
        "  background: transparent; border: 1px solid @border; border-radius: @radius2; padding: 0 20px;"
        "}"
        "QPushButton#procCancel:hover { background: @overlayHov; border-color: @textDisabled; color: @textPrimary; }"
        "QPushButton#procCancel:pressed  { background: @overlayMut; }"
        "QPushButton#procCancel:disabled { color: @textDisabled; border-color: @bgPanel; }"

    );
}

} // namespace dolphin::ui::detail
