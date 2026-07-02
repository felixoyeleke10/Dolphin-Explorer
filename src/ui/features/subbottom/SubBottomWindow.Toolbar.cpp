// SubBottomWindow.Toolbar.cpp — toolbar build and command palette item provider.

#include "ui/features/subbottom/SubBottomWindow.h"
#include "ui/features/subbottom/SubBottomView.h"
#include "ui/shared/AppCommands.h"
#include "ui/shared/widgets/ViewerToolbar.h"
#include "app/layers/DataLayer.h"

#include <QLabel>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

#include <initializer_list>

namespace dolphin::ui {

void SubBottomWindow::buildToolbar()
{
    m_toolbar = new ViewerToolbar(this);
    m_toolbar->setMetaButtonTip(
        tr("Open sub-bottom metadata, navigation, and trace information for this line."));
    m_toolbar->setCommandProvider([this] { return buildCommandItems(); });

    connect(m_toolbar, &ViewerToolbar::newRequested,      this, &SubBottomWindow::newFileRequested);
    connect(m_toolbar, &ViewerToolbar::openRequested,     this, &SubBottomWindow::openFileRequested);
    connect(m_toolbar, &ViewerToolbar::saveRequested,     this, &SubBottomWindow::saveFileRequested);
    connect(m_toolbar, &ViewerToolbar::metaRequested,     this, &SubBottomWindow::metadataRequested);
    connect(m_toolbar, &ViewerToolbar::settingsRequested, this, &SubBottomWindow::settingsRequested);

    // -- SBP-specific right section --------------------------------------------
    auto* btn_measure = m_toolbar->addButton(":/icons/measure.svg",
        tr("Measure distance on the seismic section. This tool is not enabled yet."));
    m_btn_bottom_track_tb = m_toolbar->addButton(":/icons/bottom_track.svg",
        tr("Toggle bottom track overlay. Shortcut: B."));
    m_btn_bottom_track_tb->setCheckable(true);
    m_btn_bottom_track_tb->setChecked(true);
    auto* btn_zoom = m_toolbar->addButton(":/icons/zoom.svg",
        tr("Range gate tool. This tool is not enabled yet."));

    btn_measure->setEnabled(false);
    btn_zoom->setEnabled(false);

    // Contact + feature picking are right-panel tool sections (Contact Picking /
    // Feature Drawing), not toolbar tools — see SubBottomWindow.cpp.

    qobject_cast<QVBoxLayout*>(layout())->insertWidget(0, m_toolbar);
}

QList<CommandPaletteItem> SubBottomWindow::buildCommandItems()
{
    QList<CommandPaletteItem> items;

    auto add = [&](const QString& cat, const QString& label,
                   const QString& sc,  const QString& aliases,
                   bool enabled, std::function<void()> fn)
    {
        items.append({cat, label, sc, aliases, enabled, false, std::move(fn)});
    };

    auto addCmd = [&](CommandId id, bool enabled, std::function<void()> fn) {
        items.append(toPaletteItem(id, enabled, std::move(fn)));
    };

    const bool has_layer = (m_layer != nullptr);

    addCmd(CommandId::PrevLine, has_layer && m_has_prev_line, [this] {
        emit prevLineRequested(m_layer ? m_layer->id : std::string{});
    });
    addCmd(CommandId::NextLine, has_layer && m_has_next_line, [this] {
        emit nextLineRequested(m_layer ? m_layer->id : std::string{});
    });

    addCmd(CommandId::NewProject,  true, [this] { emit newFileRequested();  });
    addCmd(CommandId::OpenProject, true, [this] { emit openFileRequested(); });
    addCmd(CommandId::SaveProject, true, [this] { emit saveFileRequested(); });

    add("View", tr("Zoom In (Traces)"),  "+", "zoom in horizontal",
        has_layer, [this] { m_view->setPxPerTrace(m_view->pxPerTrace() + 1); });
    add("View", tr("Zoom Out (Traces)"), "-", "zoom out horizontal",
        has_layer, [this] { m_view->setPxPerTrace(std::max(1, m_view->pxPerTrace() - 1)); });
    add("View", tr("Zoom In (Depth)"),   "", "zoom depth vertical",
        has_layer, [this] { m_view->setPxPerSample(m_view->pxPerSample() * 1.5f); });
    add("View", tr("Zoom Out (Depth)"),  "", "zoom depth vertical",
        has_layer, [this] { m_view->setPxPerSample(m_view->pxPerSample() / 1.5f); });

    return items;
}

} // namespace dolphin::ui
