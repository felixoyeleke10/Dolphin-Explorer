#pragma once
#include <cstdint>
#include <string>

namespace dolphin::ui {

// Reason passed to IViewerWindow::onViewerRefresh().
enum class ViewerRefreshReason : uint8_t {
    LayerDataChanged,       // a layer's on-disk data changed (reindex, etc.)
    CrsChanged,             // a layer's CRS was updated
    DisplaySettingsChanged, // app-wide display settings changed
    ProjectReplaced,        // project opened / closed / replaced
};

// Life-cycle state of the data currently held by a viewer.
// Queryable via IViewerWindow::viewerDataState().
enum class ViewerDataState : uint8_t {
    Idle,        // no layer loaded
    Loading,     // background I/O in flight
    Processing,  // background pipeline in flight (data loaded, display building)
    Ready,       // display is current
    Failed,      // last load or process ended with an error
};

// Pure interface implemented by every top-level viewer window (waterfall,
// sub-bottom, etc.). WindowRegistry calls onViewerRefresh() to broadcast
// state changes without knowing the concrete window types.
//
// layer_id is non-empty for layer-scoped events; empty for project-wide events.
class IViewerWindow {
public:
    virtual void onViewerRefresh(ViewerRefreshReason reason,
                                 const std::string&  layer_id = {}) = 0;
    virtual ViewerDataState viewerDataState() const { return ViewerDataState::Idle; }
    virtual ~IViewerWindow() = default;
};

} // namespace dolphin::ui
