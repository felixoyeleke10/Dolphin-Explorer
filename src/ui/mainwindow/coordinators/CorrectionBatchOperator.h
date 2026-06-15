#pragma once
#include "app/corrections/SidescanCorrectionParams.h"
#include "app/corrections/SbpGainParams.h"
#include "app/corrections/SbpSignalParams.h"
#include "core/ArtifactIndex.h"
#include <QObject>
#include <QString>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace dolphin::app {
class DataLayer;
class ImportService;
class Project;
class SidescanCorrectionService;
class SubBottomCorrectionService;
}

namespace dolphin::ui {
class DiagnosticsHub;
class ExecutionProgressDialog;
}

namespace dolphin::ui {

// Owns both correction services and routes all job lifecycle events through
// DiagnosticsHub and ExecutionProgressDialog so call sites only touch one object.
//
// Single-layer applies create a standalone DiagnosticsHub job.
// applyAll creates a DiagnosticsHub batch with one job per layer.
// MainWindow connects to the domain signals (correctionPersisted / correctionFailed
// / batchComplete) to update layer state and reload viewers.
class CorrectionBatchOperator : public QObject {
    Q_OBJECT
public:
    explicit CorrectionBatchOperator(app::ImportService*      import_svc,
                                     DiagnosticsHub*          hub,
                                     ExecutionProgressDialog* overlay,
                                     QObject*                 parent = nullptr);

    // Single-layer applies
    void applySSS(app::DataLayer*                        layer,
                  const std::string&                     source_path,
                  const app::SidescanCorrectionParams&   params);

    void applySBP(app::DataLayer*                layer,
                  const std::string&             source_path,
                  const app::SbpGainParams&      gain,
                  const app::SbpSignalParams&    signal);

    // Batch applies — iterate all eligible layers in the project
    void applyAllSSS(app::Project& project, const app::SidescanCorrectionParams& params);
    void applyAllSBP(app::Project& project,
                     const app::SbpGainParams&   gain,
                     const app::SbpSignalParams& signal);

    // Cancel any in-flight batch: drains the queued items immediately;
    // in-flight service calls are allowed to finish but their hub jobs are
    // marked cancelled and no batchComplete is re-emitted.
    void cancelBatch();

signals:
    // Corrected artifact written — update DataLayer path/index and reload viewer.
    void correctionPersisted(std::string layer_id,
                             std::string new_path,
                             core::ArtifactIndex new_index);
    // All corrections already baked — nothing written, no reload needed.
    void correctionSkipped(std::string layer_id);
    // Bake failed for this layer.
    void correctionFailed(std::string layer_id, QString error);
    // Emitted once after applyAllSSS / applyAllSBP completes.
    void batchComplete(int succeeded, int total);

private:
    void onSssPersisted(const std::string& lid, const std::string& path,
                        const core::ArtifactIndex& idx);
    void onSssSkipped  (const std::string& lid);
    void onSssFailed   (const std::string& lid, const std::string& error);
    void onSbpPersisted(const std::string& lid, const std::string& path,
                        const core::ArtifactIndex& idx);
    void onSbpSkipped  (const std::string& lid);
    void onSbpFailed   (const std::string& lid, const std::string& error);

    void jobPersisted(const std::string& lid, const std::string& path,
                      const core::ArtifactIndex& idx);
    void jobSkipped  (const std::string& lid);
    void jobFailed   (const std::string& lid, const QString& error);

    // Decrement pending counter for the SSS or SBP batch; fire finishBatch when done.
    void settleSss(bool succeeded);
    void settleSbp(bool succeeded);

    // Pump up to kMaxConcurrent items from the dispatch queue.
    void dispatchNextSss();
    void dispatchNextSbp();

    app::SidescanCorrectionService*  m_sss_svc = nullptr;
    app::SubBottomCorrectionService* m_sbp_svc = nullptr;
    DiagnosticsHub*                  m_hub     = nullptr;
    ExecutionProgressDialog*         m_overlay = nullptr;

    // Per-layer tracking: layer_id → DiagnosticsHub job id
    std::unordered_map<std::string, uint32_t> m_job_ids;

    // Active batch ids (0 = no batch in flight)
    uint32_t m_sss_batch_id   = 0;
    uint32_t m_sbp_batch_id   = 0;

    // Per-batch counters owned by this operator (not by the services)
    int m_sss_pending   = 0;
    int m_sss_succeeded = 0;
    int m_sss_total     = 0;
    int m_sbp_pending   = 0;
    int m_sbp_succeeded = 0;
    int m_sbp_total     = 0;

    // Layer ids enrolled in the current batch — guards against standalone
    // applySSS/applySBP results being miscounted as batch items.
    std::unordered_set<std::string> m_sss_batch_layer_ids;
    std::unordered_set<std::string> m_sbp_batch_layer_ids;

    // Deferred dispatch queue for Apply All (capped at kMaxConcurrent in flight).
    static constexpr int kMaxConcurrent = 2;
    std::deque<std::function<void()>> m_sss_queue;
    std::deque<std::function<void()>> m_sbp_queue;
    int m_sss_dispatched = 0;
    int m_sbp_dispatched = 0;
};

} // namespace dolphin::ui
