#pragma once
#include "ui/features/waterfall/WaterfallParams.h"
#include <QWidget>

class QCheckBox;
class QComboBox;
class QPushButton;
class QWidget;

namespace dolphin::ui {

class TvgCurveEditor;
class TvgEditorModeSwitch;
class WfValueRow;

// Compact gain controls (TVG / AGC / ARC).
// Edits values only; the shared bottom Apply bar reads them via writeInto() and
// applies all sections together (one rebuild). Other WaterfallParams fields are
// preserved via the m_params shadow set by setParams().
class GainControlPanel : public QWidget {
    Q_OBJECT
public:
    explicit GainControlPanel(QWidget* parent = nullptr);

public slots:
    void setParams(const WaterfallParams& p);

public:
    // Write this section's control values (TVG / AGC / ARC) into p. Used by the
    // shared bottom Apply bar to gather all sections into one params + one rebuild.
    void writeInto(WaterfallParams& p) const;

private slots:
    void updateControlStates();

private:
    // TVG
    QCheckBox*      m_tvg_en     = nullptr;
    TvgEditorModeSwitch* m_tvg_editor_mode = nullptr;
    QWidget*        m_tvg_numeric_body = nullptr;
    WfValueRow*     m_tvg_spread = nullptr;   // dB/decade
    WfValueRow*     m_tvg_absorb = nullptr;   // dB/m
    TvgCurveEditor* m_tvg_curve  = nullptr;

    // AGC — full control set (parity with the waterfall Image Processing section)
    QCheckBox*  m_agc_en           = nullptr;
    QComboBox*  m_agc_mode         = nullptr;  // 0=Global, 1=Variable
    WfValueRow* m_agc_strength     = nullptr;  // %
    QWidget*    m_agc_along_track_row = nullptr;  // visible only in Variable mode
    WfValueRow* m_agc_along_track  = nullptr;  // pings
    QComboBox*  m_agc_smooth_type  = nullptr;  // 0=Mean, 1=Median
    WfValueRow* m_agc_smooth_win   = nullptr;  // samples
    WfValueRow* m_agc_edge_skip    = nullptr;  // samples
    WfValueRow* m_agc_noise_floor  = nullptr;  // %

    // ARC
    QCheckBox*  m_arc_en       = nullptr;
    WfValueRow* m_arc_exp      = nullptr; // exponent
    WfValueRow* m_arc_gain_cap = nullptr; // dB

    WaterfallParams m_params;   // shadow copy — preserved on setParams
};

} // namespace dolphin::ui
