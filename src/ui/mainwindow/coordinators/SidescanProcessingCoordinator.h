#pragma once

#include "app/display/NavProcessingParams.h"
#include "app/display/WaterfallParams.h"

#include <QObject>
#include <QStringList>
#include <string>
#include <vector>

namespace dolphin::app { class Project; }

namespace dolphin::ui {

class DisplayStateManager;

// Single application workflow for committing SSS processing state. UI surfaces
// submit drafts here; this coordinator resolves scope, filters non-SSS layers,
// and delegates every model mutation to DisplayStateManager.
class SidescanProcessingCoordinator final : public QObject {
    Q_OBJECT
public:
    struct Result {
        std::vector<std::string> layer_ids;
        // Waterfall-derived artifacts that must be switched back to their
        // preserved imported baseline for this explicit empty processing state.
        std::vector<std::string> revert_layer_ids;
        std::vector<std::string> pipeline_changed_layer_ids;
        std::vector<std::string> display_changed_layer_ids;
        std::vector<std::string> geometry_changed_layer_ids;
        std::vector<std::string> nav_changed_layer_ids;
        bool pipeline_changed = false;
        bool geometry_changed = false;
        bool nav_changed = false;
    };

    explicit SidescanProcessingCoordinator(DisplayStateManager* display_state,
                                            QObject* parent = nullptr);

    Result commit(app::Project* project,
                  const std::vector<std::string>& requested_ids,
                  const WaterfallParams& display,
                  const NavProcessingParams* nav = nullptr);

    static std::vector<std::string> allSidescanLayerIds(const app::Project* project);

signals:
    void processingCommitted(const QStringList& layer_ids,
                             bool pipeline_changed,
                             bool geometry_changed,
                             bool nav_changed);

private:
    DisplayStateManager* m_display_state = nullptr;
};

} // namespace dolphin::ui
