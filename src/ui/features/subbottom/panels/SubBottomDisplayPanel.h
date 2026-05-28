#pragma once
#include "ui/features/subbottom/SubBottomPalette.h"
#include <QFrame>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QToolButton;
class QVBoxLayout;

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  SubBottomDisplayParams — display parameter snapshot passed through signals.
// ─────────────────────────────────────────────────────────────────────────────
struct SubBottomDisplayParams {
    int   palette_index      = SbpPalette::Greyscale;
    float gain               = 1.0f;    // amplitude multiplier before palette mapping
    float contrast           = 1.0f;    // power-curve exponent; 1 = linear, >1 = brighter mids
    bool  polarity_invert    = false;   // flip sample sign before palette mapping
    bool  show_bottom_track  = true;    // draw pre-computed seabed pick overlay
    float sound_speed_ms     = 1500.0f; // full round-trip speed (depth = time * speed / 2)
};

// ─────────────────────────────────────────────────────────────────────────────
//  SubBottomDisplayPanel — right panel in SubBottomWindow.
//
//  Three collapsible sections:
//    DISPLAY     — palette combo, gain
//    BOTTOM TRACK — show/hide toggle
//    ACQUISITION — sound speed (used by status bar depth readout)
//
//  Every control emits paramsChanged() immediately — no Apply button needed
//  because all operations are O(repaint).
// ─────────────────────────────────────────────────────────────────────────────
class SubBottomDisplayPanel : public QFrame {
    Q_OBJECT
public:
    explicit SubBottomDisplayPanel(QWidget* parent = nullptr);

    SubBottomDisplayParams currentParams() const;
    void setParams(const SubBottomDisplayParams& p);
    // Trigger paramsChanged without a UI interaction (e.g. pushed from right panel).
    void notifyParamsChanged();

signals:
    void paramsChanged(dolphin::ui::SubBottomDisplayParams params);

private:
    static QVBoxLayout* makeSection(const QString& title, bool expanded,
                                    QWidget* parent, QVBoxLayout* parent_layout);

    void emitParams();

    QComboBox*      m_palette_combo   = nullptr;
    QDoubleSpinBox* m_gain_spin       = nullptr;
    QDoubleSpinBox* m_contrast_spin   = nullptr;
    QCheckBox*      m_polarity_check  = nullptr;
    QToolButton*    m_bt_toggle       = nullptr;
    QDoubleSpinBox* m_speed_spin      = nullptr;
};

} // namespace dolphin::ui
