#pragma once
#include "ui/shell/ViewerWindow.h"
#include <QObject>
#include <QPointer>
#include <string>
#include <vector>

class QWidget;

namespace dolphin::ui {

// Registry of open viewer windows. MainWindow registers each viewer when it
// creates it; the registry auto-removes entries for destroyed widgets.
class WindowRegistry : public QObject {
    Q_OBJECT
public:
    explicit WindowRegistry(QObject* parent = nullptr);

    void registerViewer(QWidget* host, IViewerWindow* impl);
    void broadcast(ViewerRefreshReason reason, const std::string& layer_id = {});
    bool anyViewerBusy() const;

private:
    struct Entry {
        QPointer<QWidget> host;
        IViewerWindow*    impl;
    };
    std::vector<Entry> m_viewers;
};

} // namespace dolphin::ui
