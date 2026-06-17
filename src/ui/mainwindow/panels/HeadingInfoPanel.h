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

signals:
    void applyToLineRequested(const dolphin::ui::NavProcessingParams& params);
    void applyToAllRequested(const dolphin::ui::NavProcessingParams& params);

private slots:
    void onApplyLine();
    void onApplyAll();

private:
    NavProcessingParams currentParams() const;

    WfValueRow* m_hdg_offset   = nullptr;  // °
    WfValueRow* m_pitch_offset = nullptr;  // °
    WfValueRow* m_roll_offset  = nullptr;  // °
};

} // namespace dolphin::ui
