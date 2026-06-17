#pragma once
#include "ui/mainwindow/rightpanel/IRightPanelModule.h"
#include "ui/mainwindow/panels/HeadingInfoPanel.h"
#include "app/layers/DataLayer.h"

namespace dolphin::ui {

class GeometryModule : public IRightPanelModule {
public:
    // modality scopes this instance to one sensor tab (Sidescan, SubBottom, …);
    // each modality gets its own Geometry section with its own panel + wiring.
    explicit GeometryModule(app::Modality modality = app::Modality::Unknown)
        : m_modality(modality), m_panel(new HeadingInfoPanel(nullptr)) {}

    QString  title()                               const override { return QStringLiteral("Geometry"); }
    QString  icon()                                const override { return QStringLiteral(":/icons/geodesy.svg"); }
    QString  badge()                               const override {
        using M = app::Modality;
        return m_modality == M::Sidescan  ? QStringLiteral("SSS")
             : m_modality == M::SubBottom ? QStringLiteral("SBP")
             : QString{};
    }
    app::Modality primaryModality()                const override { return m_modality; }
    bool     supports(const app::DataLayer& layer) const override {
        using M = app::Modality;
        return layer.modality != M::Raster && layer.modality != M::Magnetometer;
    }
    void     setLayer(app::DataLayer*)                   override {}
    QWidget* widget()                                    override { return m_panel; }

    HeadingInfoPanel* panel() const { return m_panel; }

private:
    app::Modality     m_modality = app::Modality::Unknown;
    HeadingInfoPanel* m_panel    = nullptr;
};

} // namespace dolphin::ui
