#pragma once
#include "ui/features/waterfall/WaterfallParams.h"
#include <QWidget>

class QCheckBox;
class QPushButton;

namespace dolphin::ui {

class WfValueRow;

// Post-assembly imaging chain: ARN, Destripe, Beam Pattern, ML Enhance, SRC.
// Each section has a master toggle and the key tuning parameter.
// Changes are batched — Apply sends the full WaterfallParams.
class ImagingControlPanel : public QWidget {
    Q_OBJECT
public:
    explicit ImagingControlPanel(QWidget* parent = nullptr);

signals:
    void applyToLineRequested(const WaterfallParams& params);
    void applyToAllRequested(const WaterfallParams& params);

public slots:
    void setParams(const WaterfallParams& p);

private slots:
    void onApplyLine();
    void onApplyAll();
    void updateControlStates();

private:
    WaterfallParams buildParams() const;

    // ARN — Adaptive Range Normalisation
    QCheckBox*  m_arn_en       = nullptr;
    WfValueRow* m_arn_strength = nullptr;   // 0-1
    WfValueRow* m_arn_gain_cap = nullptr;   // dB

    // Destripe
    QCheckBox*  m_destripe_en      = nullptr;
    WfValueRow* m_destripe_capping = nullptr;  // correction factor cap

    // Beam Pattern Normalisation
    QCheckBox*  m_bpn_en       = nullptr;
    WfValueRow* m_bpn_strength = nullptr;   // 0–1

    // ML Enhance (CLAHE)
    QCheckBox*  m_ml_en         = nullptr;
    WfValueRow* m_ml_clip_limit = nullptr;  // 1–8

    // Slant Range Correction (no sub-params)
    QCheckBox*   m_src_en    = nullptr;

    QPushButton* m_apply_line_btn = nullptr;
    QPushButton* m_apply_all_btn  = nullptr;

    WaterfallParams m_params;
};

} // namespace dolphin::ui
