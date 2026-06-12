#pragma once
#include <QStatusBar>
#include <QString>

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

    // -- Context (project · active layer) ---------------------------------
    void setProjectContext(const QString& project, const QString& layer = {});
    void clearContext();

    // -- Transient job messages (auto-clear after timeout) -----------------
    void showJobMessage(const QString& msg, int timeout_ms = 8000);

    // -- Cursor data — set by whichever instrument view is active ----------
    void setCursorRange(const QString& side_label, float metres); // sidescan
    void clearCursorRange();
    void setCursorDepth(float metres);        // altitude / subbottom depth
    void clearCursorDepth();
    void setCursorPosition(double lat_or_y, double lon_or_x, bool is_projected);
    void clearCursorPosition();
    void clearCursorData();                   // clears all three fields

    // -- Progress bar ------------------------------------------------------
    void setProgressIndeterminate();          // spinner / unknown duration
    void setProgress(int percent, bool visible);
    void hideProgress();

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

    // -- Backward-compat accessors for SidescanViewController -------------
    //    Prefer the typed setters above for all MainWindow-side code.
    QLabel*       pingLabel()  const { return m_range;    }
    QLabel*       depthLabel() const { return m_depth;    }
    QLabel*       posLabel()   const { return m_pos;      }
    QProgressBar* progressBar() const { return m_progress; }

signals:
    void scaleChangeRequested(double mpp);      // user changed scale spin box
    void rotationChangeRequested(double deg);   // user changed rotation spin box
    void crsClicked();                          // user clicked the CRS badge

private:
    void rebuildAiSection();

    // Left-side non-permanent widgets
    QProgressBar* m_progress  = nullptr;
    QLabel*       m_context   = nullptr;
    QLabel*       m_job       = nullptr;
    QTimer*       m_job_timer = nullptr;

    // Right-side permanent widgets — [field label][value box] pairs
    QLabel*  m_range      = nullptr;   // cursor range or subbottom depth (sidescan)
    QLabel*  m_depth      = nullptr;   // map-hover depth / altitude reading

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
