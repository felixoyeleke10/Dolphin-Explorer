// NodeGraphWindow.PaletteDrag.cpp — palette drag/drop and placement preview arm.
#include "ui/features/nodegraph/NodeGraphWindow.h"
#include "ui/features/nodegraph/NodeGraphView.h"
#include "ui/features/nodegraph/NodeInspectorPanel.h"
#include "ui/features/nodegraph/NodePaletteView.h"
#include "pipeline/NodeRegistry.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>

namespace dolphin::ui {

void NodeGraphWindow::beginPaletteDrag(const QString& type_id,
                                       const QPoint& global_pos)
{
    if (type_id.isEmpty())
        return;

    if (!ensurePlacementTarget()) {
        if (m_inspector) {
            const bool has_project = m_project != nullptr;
            m_inspector->showStatus(
                has_project ? tr("Select a graph target first") : tr("No project available"),
                has_project
                    ? tr("Choose Project Pipeline or a layer from the top bar before dragging nodes into the graph.")
                    : tr("Create or open a project first."));
        }
        return;
    }

    QString label = type_id;
    if (auto proto = pipeline::NodeRegistry::instance().create(type_id.toStdString()))
        label = QString::fromStdString(proto->label());

    cancelPaletteDrag();
    m_palette_drag_type_id = type_id;
    m_palette_drag_live = true;
    qApp->installEventFilter(this);
    if (m_inspector) {
        m_inspector->showStatus(tr("Dragging %1").arg(label),
                                tr("Move over the graph and release to insert the node."));
    }
    updatePaletteDrag(global_pos);
}

void NodeGraphWindow::cancelPaletteDrag()
{
    if (m_palette_drag_live)
        qApp->removeEventFilter(this);

    m_palette_drag_live = false;
    m_palette_drag_type_id.clear();
    m_palette_drop_valid = false;
    m_palette_drop_pos = QPoint{};

    unsetCursor();
    if (m_canvas)
        m_canvas->clearPlacementPreview();
    if (m_palette)
        m_palette->resetInteractionState();
}

void NodeGraphWindow::updatePaletteDrag(const QPoint& global_pos)
{
    if (!m_palette_drag_live || m_palette_drag_type_id.isEmpty() || !m_canvas)
        return;

    const QPoint local_pos = m_canvas->mapFromGlobal(global_pos);
    if (!m_canvas->rect().contains(local_pos)) {
        m_palette_drop_valid = false;
        m_canvas->clearPlacementPreview();
        return;
    }

    setCursor(Qt::CrossCursor);
    m_palette_drop_valid = true;
    m_palette_drop_pos = local_pos;
    m_canvas->setPlacementPreview(m_palette_drag_type_id.toStdString(),
                                  QPointF(local_pos));
    m_canvas->setFocus();
}

void NodeGraphWindow::finishPaletteDrag(bool commit, const QPoint& global_pos)
{
    Q_UNUSED(global_pos);

    const QString type_id = m_palette_drag_type_id;
    const bool can_commit = commit
        && !type_id.isEmpty()
        && m_canvas
        && m_palette_drop_valid;

    const QPoint drop_pos = m_palette_drop_pos;

    cancelPaletteDrag();

    if (!can_commit)
        return;

    m_canvas->addNodeAtWidget(type_id.toStdString(), QPointF(drop_pos));
    m_canvas->setFocus();
}

bool NodeGraphWindow::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);

    if (!m_palette_drag_live)
        return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseMove: {
        auto* mouse_ev = static_cast<QMouseEvent*>(event);
        updatePaletteDrag(mouse_ev->globalPosition().toPoint());
        return false;
    }

    case QEvent::MouseButtonRelease: {
        auto* mouse_ev = static_cast<QMouseEvent*>(event);
        finishPaletteDrag(mouse_ev->button() == Qt::LeftButton,
                          mouse_ev->globalPosition().toPoint());
        return true;
    }

    case QEvent::KeyPress: {
        auto* key_ev = static_cast<QKeyEvent*>(event);
        if (key_ev->key() != Qt::Key_Escape)
            return false;
        cancelPaletteDrag();
        return true;
    }

    default:
        return QWidget::eventFilter(watched, event);
    }
}

} // namespace dolphin::ui
