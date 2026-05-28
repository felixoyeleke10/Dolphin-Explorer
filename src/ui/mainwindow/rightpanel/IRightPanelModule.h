#pragma once
#include "app/layers/LayerUtils.h"  // Modality
#include <QString>

namespace dolphin::app { class DataLayer; }
class QWidget;

namespace dolphin::ui {

class IRightPanelModule {
public:
    virtual ~IRightPanelModule() = default;
    virtual QString  title()                               const = 0;
    virtual QString  badge()                               const { return {}; }
    virtual QString  notApplicableHint()                   const { return {}; }
    // SVG icon resource path for the collapsible section header (e.g. ":/icons/panel_info.svg").
    virtual QString  icon()                                const { return {}; }
    // Modality this module is specific to; Unknown means "applies to all modalities".
    virtual app::Modality primaryModality()                const { return app::Modality::Unknown; }
    virtual bool     supports(const app::DataLayer& layer) const = 0;
    virtual void     setLayer(app::DataLayer* layer)             = 0;
    virtual QWidget* widget()                                    = 0;
};

} // namespace dolphin::ui

