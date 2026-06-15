// InspectorPanel.cpp — coordinator: switches between layer host, contact, and empty pages.
#include "ui/mainwindow/panels/InspectorPanel.h"
#include "ui/mainwindow/rightpanel/RightPanelHost.h"
#include "ui/shared/panels/ContactInspectorPage.h"
#include "ui/shared/UiUtils.h"
#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace dolphin::ui {

InspectorPanel::InspectorPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = makeCompactLayout<QVBoxLayout>(this);

    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack);

    m_empty = new QWidget(this);
    auto* el = new QVBoxLayout(m_empty);
    auto* lbl = new QLabel(tr("Select a layer or contact\nto view properties."), m_empty);
    lbl->setObjectName("inspectorEmpty");
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setWordWrap(true);
    el->addStretch(); el->addWidget(lbl); el->addStretch();

    m_layer   = new RightPanelHost(this);
    m_contact = new ContactInspectorPage(this);

    m_stack->addWidget(m_empty);
    m_stack->addWidget(m_layer);
    m_stack->addWidget(m_contact);

    connect(m_layer, &RightPanelHost::paletteChanged,
            this,    &InspectorPanel::paletteChanged);
    connect(m_layer, &RightPanelHost::channelChanged,
            this,    &InspectorPanel::channelChanged);

    showEmpty();
}

void InspectorPanel::showEmpty()
{
    m_stack->setCurrentWidget(m_empty);
    m_layer->clearLayer();
}

void InspectorPanel::showLayer(app::DataLayer* layer)
{
    if (!layer) { showEmpty(); return; }
    m_layer->setLayer(layer);
    m_stack->setCurrentWidget(m_layer);
}

void InspectorPanel::showContact(const core::Contact* contact)
{
    if (!contact) { showEmpty(); return; }
    m_contact->refresh(contact);
    m_stack->setCurrentWidget(m_contact);
}

int InspectorPanel::currentPaletteIndex() const
{
    return m_layer ? m_layer->currentPaletteIndex() : 0;
}

void InspectorPanel::setPalette(int idx)
{
    if (m_layer) m_layer->setPalette(idx);
}

void InspectorPanel::setChannel(DisplayChannel ch)
{
    if (m_layer) m_layer->setChannel(ch);
}

void InspectorPanel::setAvailableModalities(const QSet<app::Modality>& modalities)
{
    if (m_layer) m_layer->setAvailableModalities(modalities);
}


} // namespace dolphin::ui
