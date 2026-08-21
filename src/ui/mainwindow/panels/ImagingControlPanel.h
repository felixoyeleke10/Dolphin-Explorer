#pragma once
#include "ui/features/waterfall/WaterfallParams.h"
#include <QWidget>

class QCheckBox;
class QPushButton;

namespace dolphin::ui {

class WfValueRow;

// Post-assembly imaging chain: ARN, Destripe, Beam Pattern, Adaptive Contrast.
// Edits values only; the shared bottom Apply bar reads them via writeInto() and
// applies all sections together (one rebuild). Slant Range Correction has no control
// here — it is implied by Beam Pattern and set automatically in writeInto().
class ImagingControlPanel : public QWidget {
    Q_OBJECT
public:
    explicit ImagingControlPanel(QWidget* parent = nullptr);

public slots:
    void setParams(const WaterfallParams& p);

public:
    // Write this section's control values (ARN/destripe/BPN/CLAHE) into p; also sets
    // p.slant_range_correction from Beam Pattern (BPN requires ground-range geometry).
    // Used by the shared bottom Apply bar to gather all sections into one rebuild.
    void writeInto(WaterfallParams& p) const;

private slots:
    void updateControlStates();

private:
    // ARN — Adaptive Range Normalisation
    QCheckBox*  m_arn_en       = nullptr;
    WfValueRow* m_arn_strength = nullptr;   // 0-1
    WfValueRow* m_arn_gain_cap = nullptr;   // dB
    WfValueRow* m_arn_smooth   = nullptr;   // samples

    // Destripe (full parity with the waterfall)
    QCheckBox*  m_destripe_en      = nullptr;
    WfValueRow* m_destripe_window  = nullptr;  // pings
    WfValueRow* m_destripe_subdiv  = nullptr;  // range subdivisions
    WfValueRow* m_destripe_capping = nullptr;  // correction factor cap
    WfValueRow* m_destripe_threshold = nullptr; // dB outlier threshold

    // Beam Pattern Normalisation
    QCheckBox*  m_bpn_en       = nullptr;
    WfValueRow* m_bpn_strength = nullptr;   // 0–1
    WfValueRow* m_bpn_smooth   = nullptr;   // smoothing radius (samples)
    WfValueRow* m_bpn_gain_cap = nullptr;   // dB

    // Adaptive Contrast (CLAHE)
    QCheckBox*  m_ml_en         = nullptr;
    WfValueRow* m_ml_tile_pings = nullptr;  // along-track tile height
    WfValueRow* m_ml_tile_samps = nullptr;  // across-track tile width
    WfValueRow* m_ml_clip_limit = nullptr;  // 1–8

    WaterfallParams m_params;
};

} // namespace dolphin::ui
