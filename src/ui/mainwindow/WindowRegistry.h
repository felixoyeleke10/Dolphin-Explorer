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
//
// Callers broadcast a ViewerRefreshReason (+ optional layer_id) and every
// live, visible viewer decides whether and how to react. This replaces the
// pattern of "if (m_waterfall_win) ... if (m_sbp_win) ..." scattered across
// coordinator files.
class WindowRegistry : public QObject {
    Q_OBJECT
public:
    explicit WindowRegistry(QObject* parent = nullptr);

    // Associate a viewer implementation with its host widget.
    // The entry is silently removed when the widget is destroyed.
    void registerViewer(QWidget* host, IViewerWindow* impl);

    // Deliver a refresh event to every live registered viewer.
    void broadcast(ViewerRefreshReason reason,
                   const std::string&  layer_id = {});

    // Returns true if any live registered viewer is Loading or Processing.
    bool anyViewerBusy() const;

private:
    struct Entry {
        QPointer<QWidget> host;
        IViewerWindow*    impl;
    };
    std::vector<Entry> m_viewers;
};

} // namespace dolphin::ui
