#pragma once
#include "ui/mainwindow/rightpanel/IRightPanelModule.h"
#include "ui/mainwindow/panels/NavInfoPanel.h"
#include "app/layers/DataLayer.h"

namespace dolphin::ui {

class NavigationModule : public IRightPanelModule {
public:
    // modality scopes this instance to one sensor tab (Sidescan, SubBottom, …);
    // each modality gets its own Navigation section with its own panel + wiring.
    explicit NavigationModule(app::Modality modality = app::Modality::Unknown)
        : m_modality(modality), m_panel(new NavInfoPanel(nullptr)) {}

    QString  title()                               const override { return QStringLiteral("Navigation"); }
    QString  icon()                                const override { return QStringLiteral(":/icons/nav_editor.svg"); }
    QString  badge()                               const override {
        using M = app::Modality;
        return m_modality == M::Sidescan  ? QStringLiteral("SSS")
             : m_modality == M::SubBottom ? QStringLiteral("SBP")
             : QString{};
    }
    app::Modality primaryModality()                const override { return m_modality; }
    bool     supports(const app::DataLayer& layer) const override {
        return layer.modality != app::Modality::Raster;
    }
    void     setLayer(app::DataLayer*)                   override {}
    QWidget* widget()                                    override { return m_panel; }

    NavInfoPanel* panel() const { return m_panel; }

private:
    app::Modality m_modality = app::Modality::Unknown;
    NavInfoPanel* m_panel    = nullptr;
};

} // namespace dolphin::ui
