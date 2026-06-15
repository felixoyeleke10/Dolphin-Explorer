#pragma once
#include "ui/features/map/MapTypes.h"
#include <QObject>

namespace dolphin::ui {

class MapViewportHost;

// Single path for all viewport scale/rotation changes and notifications.
// Owns the canonical ViewportState; bridges the status bar and MapViewportHost
// so no other code needs to touch the host directly for viewport commands.
class ViewportCoordinator : public QObject {
    Q_OBJECT
public:
    explicit ViewportCoordinator(MapViewportHost* host, QObject* parent = nullptr);

    ViewportState state() const { return m_state; }

public slots:
    void requestScale   (double mpp);
    void requestRotation(double deg);

signals:
    void stateChanged(ViewportState state);

private:
    MapViewportHost* m_host;
    ViewportState    m_state;
};

} // namespace dolphin::ui
