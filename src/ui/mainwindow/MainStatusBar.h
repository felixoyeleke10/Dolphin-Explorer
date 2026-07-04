#pragma once
#include <QStatusBar>
#include <QString>

class QSpinBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QProgressBar;
class QTimer;
class QWidget;

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  MainStatusBar — modular status bar for the main window.
//
//  Layout (left → right):
//    [progress] [context · layer --------------] [job msg]
//    --- permanent ------------------------------------------------------------
//    [range / ping]  [depth]  [lat / lon]  [AI icon ●]
//
//  Callers use the typed setters below; the raw-label accessors exist only for
//  backward compatibility with SidescanViewController, which lives in a
//  different CMake target and cannot take a MainStatusBar* directly.
// -----------------------------------------------------------------------------

class MainStatusBar : public QStatusBar {
    Q_OBJECT
public:
    explicit MainStatusBar(QWidget* parent = nullptr);

    // -- Context (project name) -------------------------------------------
    void setProjectContext(const QString& project, const QString& layer = {});
    void clearContext();

    // -- Transient job messages (auto-clear after timeout) -----------------
    void showJobMessage(const QString& msg, int timeout_ms = 8000);

    // -- Cursor position — the live coordinate field (right side) ----------
    void setCursorPosition(double lat_or_y, double lon_or_x, bool is_projected);
    void clearCursorPosition();
    void clearCursorData();                   // clears the coordinate field

    // -- Progress bar ------------------------------------------------------
    void setProgressIndeterminate();          // spinner / unknown duration
    void setProgress(int percent, bool visible);
    void hideProgress();

    // -- Persistent busy label (sits next to the progress bar) -------------
    // Non-intrusive, in-window background-task feedback (e.g. "Building map…")
    // shown while a load runs and cleared when it finishes. Unlike showJobMessage
    // it does not auto-expire, so it stays for the whole operation.
    void setBusyText(const QString& text);
    void clearBusyText();

    // -- Map viewport indicators ------------------------------------------
    // Called on every zoom / fit change.  metres_per_pixel and rotation_deg
    // drive the scale, zoom-level, and bearing labels.
    void setViewportInfo(double metres_per_pixel, double rotation_deg);
    void setViewCrs(const QString& crs_name);   // set from project CRS on open/change

    // -- AI provider indicator ---------------------------------------------
    enum class AiProvider { None, Primary, Integration };
    enum class AiStatus   { Offline, Ready, Active };
    void setAiProvider(AiProvider provider);
    void setAiStatus(AiStatus status);

    // -- Accessor for SidescanViewController's live coordinate readout -----
    QLabel*       posLabel()   const { return m_pos;   }

signals:
    void scaleChangeRequested(double mpp);      // user changed scale spin box
    void rotationChangeRequested(double deg);   // user changed rotation spin box
    void crsClicked();                          // user clicked the CRS badge

private:
    void rebuildAiSection();

    // Left-side non-permanent widgets
    QProgressBar* m_progress  = nullptr;
    QLabel*       m_busy      = nullptr;   // persistent "Building map…" while loading
    QLabel*       m_context   = nullptr;
    QLabel*       m_job       = nullptr;
    QTimer*       m_job_timer = nullptr;

    // Right-side permanent widgets — [field label][value box] pairs
    QLabel*  m_lbl_coord  = nullptr;   // "Coordinate"
    QLabel*  m_pos        = nullptr;   // lat/lon display (QLabel for SidescanViewController compat)

    QLabel*         m_lbl_scale  = nullptr;   // "Scale"
    QDoubleSpinBox* m_spin_scale = nullptr;   // interactive: "1:50000"

    QLabel*         m_lbl_rot   = nullptr;    // "Rotation"
    QDoubleSpinBox* m_spin_rot  = nullptr;    // interactive: "0.0 °"

    QLabel*       m_lbl_crs  = nullptr;   // "⊙" globe glyph
    QPushButton*  m_vp_crs   = nullptr;   // clickable CRS badge → opens geodesy dialog

    QWidget* m_ai_widget  = nullptr;   // composite: icon + status dot
    QLabel*  m_ai_icon    = nullptr;
    QLabel*  m_ai_dot     = nullptr;

    AiProvider m_ai_provider = AiProvider::None;
    AiStatus   m_ai_status   = AiStatus::Offline;
};

} // namespace dolphin::ui
