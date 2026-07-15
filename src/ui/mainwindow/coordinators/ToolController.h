#pragma once

#include <QObject>
#include <QString>

class QToolButton;

namespace dolphin::ui {

class AppState;
class MapView;
class MapViewportHost;

// Owns interactive map-tool state and keeps the 2D view, 3D host, AppState,
// and exclusive toolbar buttons synchronized through one transition path.
class ToolController : public QObject {
    Q_OBJECT
public:
    explicit ToolController(AppState* app_state,
                            MapView* map_view,
                            MapViewportHost* viewport_host,
                            QObject* parent = nullptr);

    void setButtons(QToolButton* cursor,
                    QToolButton* select,
                    QToolButton* zoom,
                    QToolButton* measure,
                    QToolButton* contact,
                    QToolButton* polygon,
                    QToolButton* line,
                    QToolButton* pen);

public slots:
    void activatePan();
    void activateSelect();
    void activateZoom();
    void activateMeasure();
    void activateContactPick();
    void activateFeature(int tool); // 0=pan, 1=polygon, 2=line, 3=pen
    void toggle3D();

signals:
    void statusMessage(const QString& message);

private:
    void checkButton(QToolButton* button);

    AppState*        m_app_state = nullptr;
    MapView*         m_map_view = nullptr;
    MapViewportHost* m_viewport_host = nullptr;
    QToolButton* m_cursor = nullptr;
    QToolButton* m_select = nullptr;
    QToolButton* m_zoom = nullptr;
    QToolButton* m_measure = nullptr;
    QToolButton* m_contact = nullptr;
    QToolButton* m_polygon = nullptr;
    QToolButton* m_line = nullptr;
    QToolButton* m_pen = nullptr;
};

} // namespace dolphin::ui
