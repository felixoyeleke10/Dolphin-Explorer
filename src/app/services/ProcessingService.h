#pragma once
#include <QObject>
#include <memory>
#include <set>
#include <string>
#include "app/project/Project.h"
#include "core/ArtifactIndex.h"
#include "pipeline/GraphJob.h"

namespace dolphin::app {

// Owns the graph execution workflow.
//
// Wraps GraphRunner with Qt signals so MainWindow and panels can observe
// progress without knowing anything about the pipeline internals.
// Runs execute on background threads so the UI stays responsive.
class ProcessingService : public QObject {
    Q_OBJECT
public:
    explicit ProcessingService(QObject* parent = nullptr);
    ~ProcessingService() override;

    // Run the processing graph on a single layer asynchronously.
    void runLayer(Project& project, DataLayer* layer, const std::string& source_path);

    // Run all indexed layers in the project asynchronously.
    void runAll(Project& project);

signals:
    void runStarted  (const std::string& layer_id);
    void runComplete (const std::string& layer_id, const std::string& summary);
    void runFailed   (const std::string& layer_id, const std::string& error);
    // Emitted after a successful run when the processed artifact buffer has been
    // persisted to disk.  Consumers should update the layer's artifact_store_path
    // and artifact_index to proc_path / proc_index so viewers read processed data.
    // slant_range_corrected is true when the pipeline applied SlantRangeNode.
    void runPersisted(const std::string& layer_id,
                      const std::string& proc_path,
                      const core::ArtifactIndex& proc_index,
                      bool slant_range_corrected);
    void batchProgress(int done, int total);
    void batchComplete(int succeeded, int total);

private:
    std::set<std::string> m_active_paths; // artifact paths with an in-flight write
};

} // namespace dolphin::app
