#pragma once
#include "ui/mainwindow/rightpanel/IRightPanelModule.h"
#include "ui/mainwindow/panels/ImagingControlPanel.h"
#include "app/layers/DataLayer.h"

namespace dolphin::ui {

class EnhancementModule : public IRightPanelModule {
public:
    EnhancementModule() : m_panel(new ImagingControlPanel(nullptr)) {}

    QString  title()                               const override { return QStringLiteral("Enhancement"); }
    QString  badge()                               const override { return QStringLiteral("SSS"); }
    QString  icon()                                const override { return QStringLiteral(":/icons/panel_enhancement.svg"); }
    QString  notApplicableHint()                   const override { return QStringLiteral("Select a Sidescan layer in the Explorer to use this section."); }
    app::Modality primaryModality()                const override { return app::Modality::Sidescan; }
    bool     supports(const app::DataLayer& layer) const override {
        return layer.modality == app::Modality::Sidescan;
    }
    void     setLayer(app::DataLayer*)                   override {}
    QWidget* widget()                                    override { return m_panel; }

    ImagingControlPanel* panel() const { return m_panel; }

private:
    ImagingControlPanel* m_panel = nullptr;
};

} // namespace dolphin::ui
