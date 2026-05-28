#include "ui/bottom/RuntimeLogBridge.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <utility>
#include <vector>

namespace dolphin::ui {
namespace {

QMutex g_mutex;
RuntimeLogBridge* g_bridge = nullptr;
std::vector<std::pair<int, QString>> g_pending;

} // namespace

RuntimeLogBridge::RuntimeLogBridge(QObject* parent)
    : QObject(parent)
{
}

RuntimeLogBridge* RuntimeLogBridge::instance()
{
    QMutexLocker lock(&g_mutex);
    if (!g_bridge)
        g_bridge = new RuntimeLogBridge(QCoreApplication::instance());
    return g_bridge;
}

void RuntimeLogBridge::publish(QtMsgType type, const QString& message)
{
    RuntimeLogBridge* bridge = nullptr;
    {
        QMutexLocker lock(&g_mutex);
        bridge = g_bridge;
        if (!bridge) {
            g_pending.emplace_back(static_cast<int>(type), message);
            return;
        }
    }

    QMetaObject::invokeMethod(bridge, [bridge, type, message]() {
        emit bridge->messageLogged(static_cast<int>(type), message);
    }, Qt::QueuedConnection);
}

void RuntimeLogBridge::replayPending()
{
    std::vector<std::pair<int, QString>> pending;
    {
        QMutexLocker lock(&g_mutex);
        pending.swap(g_pending);
    }

    for (const auto& [type, message] : pending)
        emit messageLogged(type, message);
}

} // namespace dolphin::ui
