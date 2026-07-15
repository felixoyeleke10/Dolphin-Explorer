#pragma once
#include "ui/features/subbottom/SubBottomDisplayParams.h"
#include "ui/features/subbottom/SubBottomPalette.h"
#include <QFrame>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QToolButton;
class QVBoxLayout;

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  SubBottomDisplayPanel — right panel in SubBottomWindow.
//
//  Three collapsible sections:
//    DISPLAY     — palette combo, gain
//    BOTTOM TRACK — show/hide toggle
//    ACQUISITION — sound speed (used by status bar depth readout)
//
//  Every control emits paramsChanged() immediately — no Apply button needed
//  because all operations are O(repaint).
// -----------------------------------------------------------------------------
class SubBottomDisplayPanel : public QFrame {
    Q_OBJECT
public:
    explicit SubBottomDisplayPanel(QWidget* parent = nullptr);

    SubBottomDisplayParams currentParams() const;
    void setParams(const SubBottomDisplayParams& p);
    // User action: persist the current controls and update the live view.
    void notifyParamsChanged();
    // Restoration/synchronisation: update the live view without persistence.
    void refreshParams();

signals:
    void paramsChanged(dolphin::ui::SubBottomDisplayParams params);
    // Fired ONLY for user actions in this panel (persist=true paths), never for
    // programmatic synchronisation (setParams/refreshParams). The shell routes
    // this to DisplayStateManager so palette/gain edits made inside the SBP
    // window reach the map curtains, the Views panel, and the project file.
    void userParamsEdited(dolphin::ui::SubBottomDisplayParams params);

private:
    static QVBoxLayout* makeSection(const QString& title, bool expanded,
                                    QWidget* parent, QVBoxLayout* parent_layout);

    void emitParams(bool persist);

    QComboBox*      m_palette_combo   = nullptr;
    QDoubleSpinBox* m_gain_spin       = nullptr;
    QDoubleSpinBox* m_contrast_spin   = nullptr;
    QCheckBox*      m_polarity_check  = nullptr;
    QToolButton*    m_bt_toggle       = nullptr;
    QDoubleSpinBox* m_speed_spin      = nullptr;
};

} // namespace dolphin::ui
