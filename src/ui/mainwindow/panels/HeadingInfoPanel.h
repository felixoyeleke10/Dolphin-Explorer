#pragma once
#include "app/display/NavProcessingParams.h"
#include <QWidget>

namespace dolphin::ui {

class WfValueRow;

// Attitude correction settings panel.
// User sets constant heading / pitch / roll offsets to correct sensor bias,
// then applies to the current line or all lines in the project.
class HeadingInfoPanel : public QWidget {
    Q_OBJECT
public:
    explicit HeadingInfoPanel(QWidget* parent = nullptr);

    // Write this section's attitude offsets into p. Used by the shared bottom Apply
    // bar to gather all sections into one rebuild.
    void writeInto(NavProcessingParams& p) const;
    NavProcessingParams currentParams() const;

private:
    WfValueRow* m_hdg_offset   = nullptr;  // °
    WfValueRow* m_pitch_offset = nullptr;  // °
    WfValueRow* m_roll_offset  = nullptr;  // °
};

} // namespace dolphin::ui
