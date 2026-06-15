#pragma once

#include <memory>
#include <string>
#include <QObject>
#include "core/ArtifactIndex.h"

namespace dolphin::app {
class Project;
}

namespace dolphin::ui {
class CorrectionBatchOperator;
class ProcessingController;
class ProjectEventBus;
}

namespace dolphin::ui {

// Bridges processing and correction completion signals into the project's
// Worker registry, ContractStore, event bus, and persistence layer.
//
// Connect once in MainWindow after ProcessingController and
// CorrectionBatchOperator are created. On each project open/close call
// setProject() so the coordinator holds the current project pointer.
class ProjectOperationCoordinator : public QObject {
    Q_OBJECT
public:
    explicit ProjectOperationCoordinator(ProjectEventBus* event_bus,
                                         QObject*         parent = nullptr);

    void setProject(std::shared_ptr<app::Project> project);

    void connectToProcessing(ProcessingController* ctrl);
    void connectToCorrections(CorrectionBatchOperator* corr_op);

private:
    void onProcessingPersisted(const std::string& layer_id,
                               const std::string& proc_path,
                               const core::ArtifactIndex& proc_index,
                               bool slant_range_corrected);
    void onCorrectionPersisted(const std::string& layer_id,
                               const std::string& new_path,
                               const core::ArtifactIndex& new_index);

    ProjectEventBus*              m_event_bus = nullptr;
    std::shared_ptr<app::Project> m_project;
};

} // namespace dolphin::ui
