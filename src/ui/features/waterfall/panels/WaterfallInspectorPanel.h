#pragma once
#include <QFrame>
#include <string>

class QComboBox;
class QLabel;
class QPushButton;
class QToolButton;
class QVBoxLayout;

namespace dolphin::ui { class WfValueRow; }

namespace dolphin::app {
class DataLayer;
}

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  WaterfallInspectorPanel — left metadata panel in the Waterfall window.
//
//  QFrame subclass extracted from WaterfallWindow::buildInspectorPanel().
//  Owns all the survey-data, source-file, CRS, sonar, vessel, and view-
//  settings labels, plus the "Prev Line / Next Line" navigation buttons and
//  the palette combo box.
//
//  WaterfallWindow wires:
//    prevLineRequested()  → onPrevFix()
//    nextLineRequested()  → onNextFix()
//    paletteChanged()     → pushParams()
// ─────────────────────────────────────────────────────────────────────────────

class WaterfallInspectorPanel : public QFrame {
    Q_OBJECT
public:
    explicit WaterfallInspectorPanel(QWidget* parent = nullptr);

    // Refresh all labels from the data layer and current window state.
    // Pass nullptr for layer to reset everything to em-dash.
    void refresh(const app::DataLayer*  layer,
                 const std::string&     source_path,
                 uint64_t               source_size_bytes,
                 int                    total_ssc_entries,
                 float                  entries_per_row,
                 int                    samples_per_ping,
                 float                  line_length_m      = 0.f,
                 float                  ping_frequency_hz  = 0.f,
                 const std::string&     sonar_name         = {},
                 float                  sound_velocity_ms  = 0.f);

    // Palette combo — index read back by WaterfallWindow::pushParams().
    int  currentPaletteIndex() const;
    void setPalette(int idx);           // external sync — does NOT re-emit

signals:
    void prevLineRequested();
    void nextLineRequested();
    void paletteChanged(int idx);       // emitted when the palette combo changes
    void verticalScaleChanged(float);   // pings/cm  (0 = auto)
    void horizontalScaleChanged(float); // samples/cm (0 = auto)
    void setCrsRequested();             // user clicked "Set CRS" button
    void ampBarToggled(bool show);      // amplitude chart show/hide

private:
    // ── Section helpers ────────────────────────────────────────────────────
    static QVBoxLayout* makeSection(const QString& title, bool expanded,
                                    QWidget* parent, QVBoxLayout* parent_layout);

    void makeRow    (QVBoxLayout* bl, const QString& key, QLabel*& val_out,
                     const QString& obj_name = "wfMetaVal");
    void makeWideRow(QVBoxLayout* bl, const QString& key, QLabel*& val_out);

    // ── Widgets ────────────────────────────────────────────────────────────
    // Survey data
    QLabel* m_val_pings     = nullptr;
    QLabel* m_val_samples   = nullptr;
    QLabel* m_val_duration  = nullptr;
    QLabel* m_val_length    = nullptr;
    // Source file
    QLabel* m_val_filename  = nullptr;
    QLabel* m_val_fmt_size  = nullptr;
    // Coordinate system
    QLabel*      m_val_crs       = nullptr;
    QLabel*      m_val_crs_kind  = nullptr;
    QPushButton* m_btn_set_crs   = nullptr;
    // Sonar parameters
    QLabel* m_val_sonar_model = nullptr;
    QLabel* m_val_freq        = nullptr;
    QLabel* m_val_sound_spd   = nullptr;
    // Vessel
    QLabel* m_val_survey    = nullptr;
    QLabel* m_val_vessel    = nullptr;
    // View settings
    QComboBox*   m_palette_combo  = nullptr;
    WfValueRow*  m_scale_v        = nullptr;   // pings/cm  (0 = auto)
    WfValueRow*  m_scale_h        = nullptr;   // samples/cm (0 = auto)
    QToolButton* m_amp_bar_toggle = nullptr;
};

} // namespace dolphin::ui
