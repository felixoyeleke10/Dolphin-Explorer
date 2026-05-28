#pragma once
#include <QObject>
#include <memory>
#include <string>
#include "app/project/Project.h"
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
    void runStarted(const std::string& layer_id);
    void runComplete(const std::string& layer_id, const std::string& summary);
    void runFailed(const std::string& layer_id, const std::string& error);
    void batchProgress(int done, int total);
    void batchComplete(int succeeded, int total);
};

} // namespace dolphin::app
