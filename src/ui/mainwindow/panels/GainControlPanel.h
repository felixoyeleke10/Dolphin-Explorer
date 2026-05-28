#pragma once
#include "ui/features/waterfall/WaterfallParams.h"
#include <QWidget>

class QCheckBox;
class QPushButton;

namespace dolphin::ui {

class WfValueRow;

// Compact gain controls (TVG / AGC / ARC).
// Changes are batched — nothing is applied until the user clicks a button.
// Both signals carry the full WaterfallParams so all other fields
// (destripe, BPN, ML, …) are preserved from the last setParams() call.
class GainControlPanel : public QWidget {
    Q_OBJECT
public:
    explicit GainControlPanel(QWidget* parent = nullptr);

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

    // TVG
    QCheckBox*  m_tvg_en     = nullptr;
    WfValueRow* m_tvg_spread = nullptr;   // dB/decade
    WfValueRow* m_tvg_absorb = nullptr;   // dB/m

    // AGC
    QCheckBox*  m_agc_en       = nullptr;
    WfValueRow* m_agc_strength = nullptr; // %

    // ARC
    QCheckBox*  m_arc_en       = nullptr;
    WfValueRow* m_arc_exp      = nullptr; // exponent
    WfValueRow* m_arc_gain_cap = nullptr; // dB

    QPushButton* m_apply_line_btn = nullptr;
    QPushButton* m_apply_all_btn  = nullptr;

    WaterfallParams m_params;   // shadow copy — preserved on setParams
};

} // namespace dolphin::ui
