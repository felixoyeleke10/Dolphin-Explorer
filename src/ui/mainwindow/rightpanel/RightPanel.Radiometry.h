#pragma once
#include "ui/mainwindow/rightpanel/IRightPanelModule.h"
#include "ui/mainwindow/panels/GainControlPanel.h"
#include "app/layers/DataLayer.h"

namespace dolphin::ui {

class RadiometryModule : public IRightPanelModule {
public:
    RadiometryModule() : m_panel(new GainControlPanel(nullptr)) {}

    QString  title()                               const override { return QStringLiteral("Radiometry"); }
    QString  badge()                               const override { return QStringLiteral("SSS"); }
    QString  icon()                                const override { return QStringLiteral(":/icons/panel_gain.svg"); }
    QString  notApplicableHint()                   const override { return QStringLiteral("Select a Sidescan layer in the Explorer to use this section."); }
    app::Modality primaryModality()                const override { return app::Modality::Sidescan; }
    bool     supports(const app::DataLayer& layer) const override {
        return layer.modality == app::Modality::Sidescan;
    }
    void     setLayer(app::DataLayer*)                   override {}
    QWidget* widget()                                    override { return m_panel; }

    GainControlPanel* panel() const { return m_panel; }

private:
    GainControlPanel* m_panel = nullptr;
};

} // namespace dolphin::ui
