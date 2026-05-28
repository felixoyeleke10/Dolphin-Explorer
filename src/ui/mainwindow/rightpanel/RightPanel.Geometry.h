#pragma once
#include "ui/mainwindow/rightpanel/IRightPanelModule.h"
#include "ui/mainwindow/panels/HeadingInfoPanel.h"
#include "app/layers/DataLayer.h"

namespace dolphin::ui {

class GeometryModule : public IRightPanelModule {
public:
    GeometryModule() : m_panel(new HeadingInfoPanel(nullptr)) {}

    QString  title()                               const override { return QStringLiteral("Geometry"); }
    QString  icon()                                const override { return QStringLiteral(":/icons/geodesy.svg"); }
    bool     supports(const app::DataLayer& layer) const override {
        using M = app::Modality;
        return layer.modality != M::Raster && layer.modality != M::Magnetometer;
    }
    void     setLayer(app::DataLayer*)                   override {}
    QWidget* widget()                                    override { return m_panel; }

    HeadingInfoPanel* panel() const { return m_panel; }

private:
    HeadingInfoPanel* m_panel = nullptr;
};

} // namespace dolphin::ui
