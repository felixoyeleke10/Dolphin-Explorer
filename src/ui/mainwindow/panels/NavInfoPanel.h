#pragma once
#include "ui/features/waterfall/NavProcessingParams.h"
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

signals:
    void applyToLineRequested(const dolphin::ui::NavProcessingParams& params);
    void applyToAllRequested(const dolphin::ui::NavProcessingParams& params);

private slots:
    void onApplyLine();
    void onApplyAll();
    void updateControlStates();

private:
    NavProcessingParams currentParams() const;

    QCheckBox*  m_smooth_en  = nullptr;
    WfValueRow* m_smooth_win = nullptr;   // pings

    QCheckBox*  m_layback_en = nullptr;
    WfValueRow* m_layback_m  = nullptr;  // metres
};

} // namespace dolphin::ui
