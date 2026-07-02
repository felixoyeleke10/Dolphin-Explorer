#pragma once
#include "app/display/NavProcessingParams.h"
#include <QWidget>

class QCheckBox;

namespace dolphin::ui {

class WfValueRow;

// Nav position correction settings panel.
// User configures GPS smoothing and towfish layback, then applies to
// the current line or all lines in the project.
class NavInfoPanel : public QWidget {
    Q_OBJECT
public:
    explicit NavInfoPanel(QWidget* parent = nullptr);

    // Write this section's nav controls (smoothing / layback) into p. Used by the
    // shared bottom Apply bar to gather all sections into one rebuild.
    void writeInto(NavProcessingParams& p) const;
    NavProcessingParams currentParams() const;

private slots:
    void updateControlStates();

private:
    QCheckBox*  m_smooth_en  = nullptr;
    WfValueRow* m_smooth_win = nullptr;   // pings

    QCheckBox*  m_layback_en = nullptr;
    WfValueRow* m_layback_m  = nullptr;  // metres
};

} // namespace dolphin::ui
