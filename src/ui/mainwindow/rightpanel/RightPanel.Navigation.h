#pragma once
#include "ui/mainwindow/rightpanel/IRightPanelModule.h"
#include "ui/mainwindow/panels/NavInfoPanel.h"
#include "app/layers/DataLayer.h"

namespace dolphin::ui {

class NavigationModule : public IRightPanelModule {
public:
    NavigationModule() : m_panel(new NavInfoPanel(nullptr)) {}

    QString  title()                               const override { return QStringLiteral("Navigation"); }
    QString  icon()                                const override { return QStringLiteral(":/icons/nav_editor.svg"); }
    bool     supports(const app::DataLayer& layer) const override {
        return layer.modality != app::Modality::Raster;
    }
    void     setLayer(app::DataLayer*)                   override {}
    QWidget* widget()                                    override { return m_panel; }

    NavInfoPanel* panel() const { return m_panel; }

private:
    NavInfoPanel* m_panel = nullptr;
};

} // namespace dolphin::ui
