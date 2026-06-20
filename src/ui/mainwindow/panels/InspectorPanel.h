#pragma once
#include "app/layers/DataLayer.h"
#include "app/layers/LayerUtils.h"
#include "core/Contact.h"
#include <QWidget>

class QStackedWidget;

namespace dolphin::ui {

class RightPanelHost;
class ContactInspectorPage;

// Thin coordinator: switches between layer, contact, and empty pages.
// Layer content is hosted inside RightPanelHost (module-based right panel).
class InspectorPanel : public QWidget {
    Q_OBJECT
public:
    explicit InspectorPanel(QWidget* parent = nullptr);

    void showLayer  (app::DataLayer*      layer);
    void showContact(const core::Contact* contact);
    void showEmpty  ();

    RightPanelHost* rightPanelHost() const { return m_layer; }

    int  currentPaletteIndex() const;
    void setPalette(int idx);

    // Preferred height of the *currently shown* page (layer / contact / empty),
    // not the max over all pages — lets the host pane hug its real content.
    int  contentHeight() const;

signals:
    void paletteChanged(int idx);

private:
    QStackedWidget*       m_stack   = nullptr;
    QWidget*              m_empty   = nullptr;
    RightPanelHost*       m_layer   = nullptr;
    ContactInspectorPage* m_contact = nullptr;
};

} // namespace dolphin::ui
