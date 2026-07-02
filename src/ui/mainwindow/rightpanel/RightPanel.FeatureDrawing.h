#pragma once
#include "ui/mainwindow/rightpanel/IRightPanelModule.h"
#include "ui/shared/panels/FeatureDrawingPanel.h"
#include "app/layers/DataLayer.h"

namespace dolphin::ui {

// "Feature Drawing" right-panel section. Universal (Unknown modality) — the tool
// applies to the map regardless of the active sensor, so it shows on every tab.
class FeatureDrawingModule : public IRightPanelModule {
public:
    FeatureDrawingModule() : m_panel(new FeatureDrawingPanel(nullptr)) {}

    QString  title()                               const override { return QStringLiteral("Feature Drawing"); }
    QString  icon()                                const override { return QStringLiteral(":/icons/feature.svg"); }
    app::Modality primaryModality()                const override { return app::Modality::Unknown; }
    bool     supports(const app::DataLayer&)       const override { return true; }
    void     setLayer(app::DataLayer*)                   override {}
    QWidget* widget()                                    override { return m_panel; }

    FeatureDrawingPanel* panel() const { return m_panel; }

private:
    FeatureDrawingPanel* m_panel = nullptr;
};

} // namespace dolphin::ui
