// CommandBar.cpp
#include "ui/shared/widgets/CommandBar.h"

#include <QEvent>

namespace dolphin::ui {

CommandBar::CommandBar(QWidget* parent)
    : QLineEdit(parent)
{
    setReadOnly(true);
    setPlaceholderText(tr("Search commands…"));
    setCursor(Qt::PointingHandCursor);
    setObjectName("commandBar");
    // Palette is created lazily in open() so window() is valid at that point.
}

void CommandBar::setProvider(std::function<QList<CommandPaletteItem>()> fn)
{
    m_provider = std::move(fn);
}

void CommandBar::setAnchorWidget(QWidget* anchor)
{
    m_anchor = anchor;
}

void CommandBar::open()
{
    if (!m_palette)
        m_palette = new CommandPaletteDialog(window());

    if (m_palette->isVisible()) return;

    if (m_provider)
        m_palette->setItems(m_provider());

    // Anchor to the override widget if set, otherwise to this bar itself.
    // The palette drops directly below the anchor and matches its width.
    m_palette->popup(m_anchor ? m_anchor : this);
}

bool CommandBar::event(QEvent* ev)
{
    if (ev->type() == QEvent::MouseButtonPress) {
        open();
        return true;
    }
    return QLineEdit::event(ev);
}

} // namespace dolphin::ui
