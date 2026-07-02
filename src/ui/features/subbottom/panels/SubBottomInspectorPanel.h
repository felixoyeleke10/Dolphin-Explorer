#pragma once
#include <QFrame>
#include <string>
#include <utility>
#include <vector>

class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QSpinBox;
class QToolButton;
class QVBoxLayout;

namespace dolphin::app { class DataLayer; }

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  SubBottomInspectorPanel — left metadata panel in SubBottomWindow.
//
//  Six collapsible sections (mirroring WaterfallInspectorPanel):
//    SURVEY DATA   — trace count, samples, duration, line length
//    SOURCE FILE   — filename + format / size
//    COORDINATE SYSTEM — CRS name + kind
//    SONAR         — frequency, sound speed
//    VESSEL        — survey name, vessel name
//    VIEW SETTINGS — trace width (px/trace), depth scale (px/sample)
//
//  Prev/Next navigation buttons are pinned below the scroll area.
// -----------------------------------------------------------------------------
class SubBottomInspectorPanel : public QFrame {
    Q_OBJECT
public:
    explicit SubBottomInspectorPanel(QWidget* parent = nullptr);

    void refresh(const app::DataLayer* layer,
                 const std::string&    source_path,
                 uint64_t              source_size_bytes,
                 int                   trace_count,
                 int                   samples_per_trace,
                 float                 duration_s,
                 float                 line_length_m,
                 float                 frequency_hz,
                 float                 sound_speed_ms);

    // Sync VIEW SETTINGS spinboxes without re-emitting signals (called on wheel zoom).
    void setViewScale(int px_per_trace, float px_per_sample);

    // Populate the LINES list with all SBP layers in the project.
    void setProjectLayers(const std::vector<std::pair<std::string, std::string>>& layers);
    // Highlight the active line (does not emit layerChangeRequested).
    void setActiveLine(const std::string& id);

    // Enable/disable the Prev/Next Line buttons (disabled at the ends of the list).
    void setNavEnabled(bool has_prev, bool has_next);

signals:
    void prevLineRequested();
    void nextLineRequested();
    void traceWidthChanged(int px_per_trace);
    void depthScaleChanged(float px_per_sample);
    void layerChangeRequested(const std::string& id);

private:
    static QVBoxLayout* makeSection(const QString& title, bool expanded,
                                    QWidget* parent, QVBoxLayout* parent_layout);
    void makeRow    (QVBoxLayout* bl, const QString& key, QLabel*& val_out);
    void makeWideRow(QVBoxLayout* bl, const QString& key, QLabel*& val_out);

    // -- Survey data -------------------------------------------------------
    QLabel* m_val_traces   = nullptr;
    QLabel* m_val_samples  = nullptr;
    QLabel* m_val_duration = nullptr;
    QLabel* m_val_length   = nullptr;
    // -- Source file -------------------------------------------------------
    QLabel* m_val_filename = nullptr;
    QLabel* m_val_fmt_size = nullptr;
    // -- Coordinate system -------------------------------------------------
    QLabel* m_val_crs      = nullptr;
    QLabel* m_val_crs_kind = nullptr;
    // -- Sonar -------------------------------------------------------------
    QLabel* m_val_freq     = nullptr;
    QLabel* m_val_speed    = nullptr;
    // -- Vessel ------------------------------------------------------------
    QLabel* m_val_survey   = nullptr;
    QLabel* m_val_vessel   = nullptr;
    // -- View settings -----------------------------------------------------
    QSpinBox*       m_trace_width_spin  = nullptr;  // px per trace column [1, 20]
    QDoubleSpinBox* m_depth_scale_spin  = nullptr;  // px per sample [0.05, 5.0]
    // -- Lines list ------------------------------------------------------------
    QListWidget*    m_lines_list        = nullptr;
    // -- Line navigation buttons ----------------------------------------------
    QToolButton*    m_btn_prev          = nullptr;
    QToolButton*    m_btn_next          = nullptr;
};

} // namespace dolphin::ui
