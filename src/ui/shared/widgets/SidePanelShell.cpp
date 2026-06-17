#include "ui/shared/widgets/SidePanelShell.h"
#include "ui/shared/UiUtils.h"

#include <QVBoxLayout>

namespace dolphin::ui {

SidePanelShell::SidePanelShell(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("sidePanelShell"));
    m_layout = makeCompactLayout<QVBoxLayout>(this);
}

void SidePanelShell::setHeader(QWidget* header)
{
    if (m_header == header) return;
    if (m_header) {
        m_layout->removeWidget(m_header);
        m_header->deleteLater();
    }
    m_header = header;
    if (m_header) {
        m_header->setParent(this);
        m_layout->insertWidget(0, m_header, 0);
    }
}

void SidePanelShell::setBody(QWidget* body)
{
    if (m_body == body) return;
    if (m_body) {
        m_layout->removeWidget(m_body);
        m_body->deleteLater();
    }
    m_body = body;
    if (m_body) {
        m_body->setParent(this);
        m_layout->addWidget(m_body, 1);
    }
}

} // namespace dolphin::ui
