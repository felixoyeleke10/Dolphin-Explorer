#pragma once
#include "ui/mainwindow/rightpanel/IRightPanelModule.h"
#include "ui/shared/panels/ContactPickingPanel.h"
#include "app/layers/DataLayer.h"

namespace dolphin::ui {

// "Contact Picking" right-panel section. Universal (Unknown modality) — the tool
// applies to the map regardless of the active sensor, so it shows on every tab.
class ContactPickingModule : public IRightPanelModule {
public:
    ContactPickingModule() : m_panel(new ContactPickingPanel(nullptr)) {}

    QString  title()                               const override { return QStringLiteral("Contact Picking"); }
    QString  icon()                                const override { return QStringLiteral(":/icons/add_contact.svg"); }
    app::Modality primaryModality()                const override { return app::Modality::Unknown; }
    bool     supports(const app::DataLayer&)       const override { return true; }
    void     setLayer(app::DataLayer*)                   override {}
    QWidget* widget()                                    override { return m_panel; }

    ContactPickingPanel* panel() const { return m_panel; }

private:
    ContactPickingPanel* m_panel = nullptr;
};

} // namespace dolphin::ui
