#include "ui/mainwindow/coordinators/ViewportCoordinator.h"
#include "ui/features/map/MapViewportHost.h"

namespace dolphin::ui {

ViewportCoordinator::ViewportCoordinator(MapViewportHost* host, QObject* parent)
    : QObject(parent)
    , m_host(host)
{
    connect(m_host, &MapViewportHost::viewportChanged,
            this, [this](double mpp, double rot) {
                m_state = { mpp, rot };
                emit stateChanged(m_state);
            });
}

void ViewportCoordinator::requestScale(double mpp)
{
    if (mpp <= 0.0) return;
    m_state.mpp = mpp;
    m_host->setViewportScale(mpp);
}

void ViewportCoordinator::requestRotation(double deg)
{
    m_state.rot_deg = deg;
    m_host->setRotationDeg(deg);
}

} // namespace dolphin::ui
