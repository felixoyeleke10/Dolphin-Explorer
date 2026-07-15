#include "ui/mainwindow/coordinators/ToolController.h"

#include "ui/systems/AppState.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapViewportHost.h"

#include <QSignalBlocker>
#include <QToolButton>

namespace dolphin::ui {

ToolController::ToolController(AppState* app_state,
                               MapView* map_view,
                               MapViewportHost* viewport_host,
                               QObject* parent)
    : QObject(parent)
    , m_app_state(app_state)
    , m_map_view(map_view)
    , m_viewport_host(viewport_host)
{}

void ToolController::setButtons(QToolButton* cursor,
                                QToolButton* select,
                                QToolButton* zoom,
                                QToolButton* measure,
                                QToolButton* contact,
                                QToolButton* polygon,
                                QToolButton* line,
                                QToolButton* pen)
{
    m_cursor = cursor;
    m_select = select;
    m_zoom = zoom;
    m_measure = measure;
    m_contact = contact;
    m_polygon = polygon;
    m_line = line;
    m_pen = pen;
}

void ToolController::checkButton(QToolButton* button)
{
    if (!button) return;
    const QSignalBlocker blocker(button);
    button->setChecked(true);
}

void ToolController::activatePan()
{
    if (m_map_view) m_map_view->setInputMode(MapView::ModePan);
    if (m_viewport_host) m_viewport_host->setToolMode(ToolMode::Pan);
    checkButton(m_cursor);
    if (m_app_state) m_app_state->setToolMode(ToolMode::Pan);
}

void ToolController::activateSelect()
{
    if (m_map_view) m_map_view->setInputMode(MapView::ModeSelect);
    if (m_viewport_host) m_viewport_host->setToolMode(ToolMode::Select);
    checkButton(m_select);
    if (m_app_state) m_app_state->setToolMode(ToolMode::Select);
}

void ToolController::activateZoom()
{
    if (m_map_view) m_map_view->setInputMode(MapView::ModeZoom);
    if (m_viewport_host) m_viewport_host->setToolMode(ToolMode::Zoom);
    checkButton(m_zoom);
    if (m_app_state) m_app_state->setToolMode(ToolMode::Zoom);
}

void ToolController::activateMeasure()
{
    if (m_map_view) m_map_view->setInputMode(MapView::ModeMeasure);
    if (m_viewport_host) m_viewport_host->setToolMode(ToolMode::Measure);
    checkButton(m_measure);
    if (m_app_state) m_app_state->setToolMode(ToolMode::Measure);
    emit statusMessage(tr("Click to add points. Right-click or double-click to reset."));
}

void ToolController::activateContactPick()
{
    if (m_map_view) m_map_view->setInputMode(MapView::ModePickContact);
    if (m_viewport_host) m_viewport_host->setToolMode(ToolMode::ContactPick);
    checkButton(m_contact);
    if (m_app_state) m_app_state->setToolMode(ToolMode::ContactPick);
    emit statusMessage(
        tr("Click on the map to place a contact. Press V or another tool to stop."));
}

void ToolController::activateFeature(int tool)
{
    if (tool <= 0) {
        activatePan();
        return;
    }
    if (m_viewport_host && m_viewport_host->isMode3D())
        m_viewport_host->setMode3D(false);
    if (m_map_view) {
        m_map_view->setFeatureDrawKind(tool);
        m_map_view->setInputMode(MapView::ModeDrawFeature);
    }
    checkButton(tool == 1 ? m_polygon : tool == 2 ? m_line : m_pen);
    emit statusMessage(
        tool == 1 ? tr("Polygon: click points, double-click or Enter to close, Esc to cancel.")
      : tool == 2 ? tr("Line: click points, double-click or Enter to finish, Esc to cancel.")
                  : tr("Pen: press and drag to draw freehand; release to finish."));
}

void ToolController::toggle3D()
{
    if (m_viewport_host)
        m_viewport_host->setMode3D(!m_viewport_host->isMode3D());
}

} // namespace dolphin::ui
