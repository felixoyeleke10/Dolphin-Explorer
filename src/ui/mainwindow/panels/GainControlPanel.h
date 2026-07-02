#pragma once
#include "ui/features/waterfall/WaterfallParams.h"
#include <QWidget>

class QCheckBox;
class QPushButton;

namespace dolphin::ui {

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

    WaterfallParams m_params;   // shadow copy — preserved on setParams
};

} // namespace dolphin::ui
