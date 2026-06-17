#include "ui/mainwindow/coordinators/CorrectionBatchOperator.h"
#include "app/corrections/SidescanCorrectionService.h"
#include "app/corrections/SubBottomCorrectionService.h"
#include "app/layers/DataLayer.h"
#include "app/display/WaterfallParams.h"   // toCorrectionParams (per-layer SSS bake)
#include "app/project/Project.h"
#include "ui/bottom/DiagnosticsHub.h"
#include "ui/features/import/ImportProgressDialog.h"

namespace dolphin::ui {

CorrectionBatchOperator::CorrectionBatchOperator(app::ImportService*      import_svc,
                                                  DiagnosticsHub*          hub,
                                                  ExecutionProgressDialog* overlay,
                                                  QObject*                 parent)
    : QObject(parent)
    , m_sss_svc(new app::SidescanCorrectionService(import_svc, this))
    , m_sbp_svc(new app::SubBottomCorrectionService(import_svc, this))
    , m_hub(hub)
    , m_overlay(overlay)
{
    connect(m_sss_svc, &app::SidescanCorrectionService::correctionsPersisted,
            this, &CorrectionBatchOperator::onSssPersisted);
    connect(m_sss_svc, &app::SidescanCorrectionService::applySkipped,
            this, &CorrectionBatchOperator::onSssSkipped);
    connect(m_sss_svc, &app::SidescanCorrectionService::applyFailed,
            this, &CorrectionBatchOperator::onSssFailed);

    connect(m_sbp_svc, &app::SubBottomCorrectionService::correctionsPersisted,
            this, &CorrectionBatchOperator::onSbpPersisted);
    connect(m_sbp_svc, &app::SubBottomCorrectionService::applySkipped,
            this, &CorrectionBatchOperator::onSbpSkipped);
    connect(m_sbp_svc, &app::SubBottomCorrectionService::applyFailed,
            this, &CorrectionBatchOperator::onSbpFailed);
}

// -- Single-layer applies -----------------------------------------------------

void CorrectionBatchOperator::applySSS(app::DataLayer*                      layer,
                                        const std::string&                   source_path,
                                        const app::SidescanCorrectionParams& params,
                                        std::vector<core::SidescanPing>      viewer_pings)
{
    if (!layer) return;
    const std::string& lid   = layer->id;
    const QString      label = QString::fromStdString(layer->label);

    if (m_job_ids.count(lid) > 0 || m_sss_batch_layer_ids.count(lid) > 0) {
        m_hub->logOutput(tr("Correction already in progress or queued for %1 — skipped").arg(label));
        return;
    }

    const uint32_t jid = m_hub->beginJob(
        tr("Baking corrections into %1…").arg(label),
        QString::fromStdString(lid), 0, QStringLiteral("COR"));
    m_job_ids[lid] = jid;
    m_overlay->addJob(lid, label, QStringLiteral("COR"), 0.f);

    m_sss_svc->applyToLine(lid, layer->artifact_store_path, layer->artifact_store_format,
                            layer->artifact_index, source_path, params,
                            std::move(viewer_pings));
}

void CorrectionBatchOperator::applySBP(app::DataLayer*             layer,
                                        const std::string&          source_path,
                                        const app::SbpGainParams&   gain,
                                        const app::SbpSignalParams& signal)
{
    if (!layer) return;
    const std::string& lid   = layer->id;
    const QString      label = QString::fromStdString(layer->label);

    if (m_job_ids.count(lid) > 0 || m_sbp_batch_layer_ids.count(lid) > 0) {
        m_hub->logOutput(tr("Correction already in progress or queued for %1 — skipped").arg(label));
        return;
    }

    const uint32_t jid = m_hub->beginJob(
        tr("Baking corrections into %1…").arg(label),
        QString::fromStdString(lid), 0, QStringLiteral("SBP"));
    m_job_ids[lid] = jid;
    m_overlay->addJob(lid, label, QStringLiteral("SBP"), 0.f);

    m_sbp_svc->applyToLine(lid, layer->artifact_store_path, layer->artifact_store_format,
                            layer->artifact_index, source_path, gain, signal);
}

// -- Bake all customized layers ----------------------------------------------
// Each layer bakes its OWN display state (per-layer params), dispatched through the
// kMaxConcurrent-capped queue so heavy read+correct+write jobs honour D-14. Sets up
// an SSS batch and/or an SBP batch depending on which modalities have work.

void CorrectionBatchOperator::bakeCustomized(app::Project& project)
{
    // -- SSS: one job per customized sidescan layer ---------------------------
    if (m_sss_batch_id != 0) {
        m_hub->logOutput(tr("SSS correction batch already in progress — request ignored"));
    } else {
        int total = 0;
        for (const auto& l : project.layers()) {
            if (!l || l->modality != app::Modality::Sidescan) continue;
            if (!l->sss_display_state.customized) continue;
            if (!l->index_built || l->sidescanCount() == 0) continue;
            if (m_job_ids.count(l->id) > 0) {
                m_hub->logOutput(tr("Correction already in progress for %1 — skipped in batch")
                    .arg(QString::fromStdString(l->label)));
                continue;
            }
            ++total;
        }
        if (total > 0) {
            m_sss_total      = total;
            m_sss_pending    = total;
            m_sss_succeeded  = 0;
            m_sss_dispatched = 0;
            m_sss_batch_layer_ids.clear();
            m_sss_queue.clear();
            m_sss_batch_id = m_hub->beginBatch(
                tr("Baking SSS corrections (%1 lines)").arg(total), total);
            const uint32_t batch_id = m_sss_batch_id;

            for (const auto& l : project.layers()) {
                if (!l || l->modality != app::Modality::Sidescan) continue;
                if (!l->sss_display_state.customized) continue;
                if (!l->index_built || l->sidescanCount() == 0) continue;
                if (m_job_ids.count(l->id) > 0) continue; // already guarded above

                m_sss_batch_layer_ids.insert(l->id);

                // Capture all data by value; the per-layer params come from the
                // layer's own display state. Hub job is created inside the lambda so
                // only dispatched items (≤ kMaxConcurrent) appear as [RUNNING].
                const std::string lid        = l->id;
                const std::string label_s    = l->label;
                const std::string store_path = l->artifact_store_path;
                const std::string store_fmt  = l->artifact_store_format;
                const core::ArtifactIndex ai = l->artifact_index;
                const auto* src = project.findSource(l->source_id);
                const std::string src_path   = src ? src->path : std::string{};
                const app::SidescanCorrectionParams params =
                    toCorrectionParams(l->sss_display_state.params);

                m_sss_queue.push_back([this, lid, label_s, store_path, store_fmt,
                                        ai, src_path, batch_id, params]() {
                    const QString label = QString::fromStdString(label_s);
                    const uint32_t jid  = m_hub->beginJob(
                        tr("Baking corrections into %1…").arg(label),
                        QString::fromStdString(lid), batch_id, QStringLiteral("COR"));
                    m_job_ids[lid] = jid;
                    m_overlay->addJob(lid, label, QStringLiteral("COR"), 0.f);
                    m_sss_svc->applyToLine(lid, store_path, store_fmt, ai, src_path, params);
                });
            }
            dispatchNextSss();
        }
    }

    // -- SBP: one job per customized sub-bottom layer -------------------------
    if (m_sbp_batch_id != 0) {
        m_hub->logOutput(tr("SBP correction batch already in progress — request ignored"));
    } else {
        int total = 0;
        for (const auto& l : project.layers()) {
            if (!l || l->modality != app::Modality::SubBottom) continue;
            if (!(l->sbp_display_state.gain_customized
                  || l->sbp_display_state.signal_customized)) continue;
            if (!l->index_built || l->subBottomCount() == 0) continue;
            if (m_job_ids.count(l->id) > 0) {
                m_hub->logOutput(tr("Correction already in progress for %1 — skipped in batch")
                    .arg(QString::fromStdString(l->label)));
                continue;
            }
            ++total;
        }
        if (total > 0) {
            m_sbp_total      = total;
            m_sbp_pending    = total;
            m_sbp_succeeded  = 0;
            m_sbp_dispatched = 0;
            m_sbp_batch_layer_ids.clear();
            m_sbp_queue.clear();
            m_sbp_batch_id = m_hub->beginBatch(
                tr("Baking SBP corrections (%1 lines)").arg(total), total);
            const uint32_t batch_id = m_sbp_batch_id;

            for (const auto& l : project.layers()) {
                if (!l || l->modality != app::Modality::SubBottom) continue;
                if (!(l->sbp_display_state.gain_customized
                      || l->sbp_display_state.signal_customized)) continue;
                if (!l->index_built || l->subBottomCount() == 0) continue;
                if (m_job_ids.count(l->id) > 0) continue;

                m_sbp_batch_layer_ids.insert(l->id);

                const std::string lid        = l->id;
                const std::string label_s    = l->label;
                const std::string store_path = l->artifact_store_path;
                const std::string store_fmt  = l->artifact_store_format;
                const core::ArtifactIndex ai = l->artifact_index;
                const auto* src = project.findSource(l->source_id);
                const std::string src_path   = src ? src->path : std::string{};
                const app::SbpGainParams   gain   = l->sbp_display_state.gain;
                const app::SbpSignalParams signal = l->sbp_display_state.signal;

                m_sbp_queue.push_back([this, lid, label_s, store_path, store_fmt,
                                        ai, src_path, batch_id, gain, signal]() {
                    const QString label = QString::fromStdString(label_s);
                    const uint32_t jid  = m_hub->beginJob(
                        tr("Baking corrections into %1…").arg(label),
                        QString::fromStdString(lid), batch_id, QStringLiteral("SBP"));
                    m_job_ids[lid] = jid;
                    m_overlay->addJob(lid, label, QStringLiteral("SBP"), 0.f);
                    m_sbp_svc->applyToLine(lid, store_path, store_fmt, ai, src_path, gain, signal);
                });
            }
            dispatchNextSbp();
        }
    }
}

// -- Private settlement helpers -----------------------------------------------

void CorrectionBatchOperator::onSssPersisted(const std::string& lid,
                                              const std::string& path,
                                              const core::ArtifactIndex& idx)
{
    jobPersisted(lid, path, idx);
    if (m_sss_batch_layer_ids.erase(lid) > 0)
        settleSss(true);
}

void CorrectionBatchOperator::onSssSkipped(const std::string& lid)
{
    jobSkipped(lid);
    if (m_sss_batch_layer_ids.erase(lid) > 0)
        settleSss(true); // skipped is not a failure
}

void CorrectionBatchOperator::onSssFailed(const std::string& lid,
                                           const std::string& error)
{
    jobFailed(lid, QString::fromStdString(error));
    if (m_sss_batch_layer_ids.erase(lid) > 0)
        settleSss(false);
}

void CorrectionBatchOperator::onSbpPersisted(const std::string& lid,
                                              const std::string& path,
                                              const core::ArtifactIndex& idx)
{
    jobPersisted(lid, path, idx);
    if (m_sbp_batch_layer_ids.erase(lid) > 0)
        settleSbp(true);
}

void CorrectionBatchOperator::onSbpSkipped(const std::string& lid)
{
    jobSkipped(lid);
    if (m_sbp_batch_layer_ids.erase(lid) > 0)
        settleSbp(true);
}

void CorrectionBatchOperator::onSbpFailed(const std::string& lid,
                                           const std::string& error)
{
    jobFailed(lid, QString::fromStdString(error));
    if (m_sbp_batch_layer_ids.erase(lid) > 0)
        settleSbp(false);
}

void CorrectionBatchOperator::settleSss(bool succeeded)
{
    --m_sss_dispatched;
    if (succeeded) ++m_sss_succeeded;
    --m_sss_pending;

    dispatchNextSss(); // pump next queued item (no-op if queue was drained by cancel)

    const int done = m_sss_total - m_sss_pending;
    if (m_sss_pending > 0) {
        if (m_sss_batch_id != 0)
            m_hub->updateBatch(m_sss_batch_id, done, m_sss_succeeded);
    } else {
        // All items settled — normal completion or tail after cancel.
        if (m_sss_batch_id != 0) {
            const int ok = m_sss_succeeded, n = m_sss_total;
            m_hub->finishBatch(m_sss_batch_id, ok, n);
            m_sss_batch_id = 0;
            emit batchComplete(ok, n);
        }
        m_sss_batch_layer_ids.clear();
    }
}

void CorrectionBatchOperator::settleSbp(bool succeeded)
{
    --m_sbp_dispatched;
    if (succeeded) ++m_sbp_succeeded;
    --m_sbp_pending;

    dispatchNextSbp();

    const int done = m_sbp_total - m_sbp_pending;
    if (m_sbp_pending > 0) {
        if (m_sbp_batch_id != 0)
            m_hub->updateBatch(m_sbp_batch_id, done, m_sbp_succeeded);
    } else {
        if (m_sbp_batch_id != 0) {
            const int ok = m_sbp_succeeded, n = m_sbp_total;
            m_hub->finishBatch(m_sbp_batch_id, ok, n);
            m_sbp_batch_id = 0;
            emit batchComplete(ok, n);
        }
        m_sbp_batch_layer_ids.clear();
    }
}

void CorrectionBatchOperator::cancelBatch()
{
    // SSS batch — drain queued items first, then mark hub batch cancelled.
    if (m_sss_batch_id != 0) {
        m_sss_pending -= static_cast<int>(m_sss_queue.size());
        m_sss_queue.clear();
        m_hub->cancelBatch(m_sss_batch_id);
        m_sss_batch_id = 0;
        // If nothing is in-flight, clean up now; otherwise let settling do it.
        if (m_sss_dispatched == 0)
            m_sss_batch_layer_ids.clear();
    }

    // SBP batch — same pattern.
    if (m_sbp_batch_id != 0) {
        m_sbp_pending -= static_cast<int>(m_sbp_queue.size());
        m_sbp_queue.clear();
        m_hub->cancelBatch(m_sbp_batch_id);
        m_sbp_batch_id = 0;
        if (m_sbp_dispatched == 0)
            m_sbp_batch_layer_ids.clear();
    }
}

void CorrectionBatchOperator::dispatchNextSss()
{
    while (m_sss_dispatched < kMaxConcurrent && !m_sss_queue.empty()) {
        auto fn = std::move(m_sss_queue.front());
        m_sss_queue.pop_front();
        ++m_sss_dispatched;
        fn();
    }
}

void CorrectionBatchOperator::dispatchNextSbp()
{
    while (m_sbp_dispatched < kMaxConcurrent && !m_sbp_queue.empty()) {
        auto fn = std::move(m_sbp_queue.front());
        m_sbp_queue.pop_front();
        ++m_sbp_dispatched;
        fn();
    }
}

void CorrectionBatchOperator::jobPersisted(const std::string& lid,
                                            const std::string& path,
                                            const core::ArtifactIndex& idx)
{
    m_overlay->finishJob(lid, tr("Corrections applied"));
    const auto it = m_job_ids.find(lid);
    if (it != m_job_ids.end()) {
        m_hub->endJob(it->second, tr("Corrections applied"));
        m_job_ids.erase(it);
    }
    emit correctionPersisted(lid, path, idx);
}

void CorrectionBatchOperator::jobSkipped(const std::string& lid)
{
    m_overlay->finishJob(lid, tr("Already baked — skipped"));
    const auto it = m_job_ids.find(lid);
    if (it != m_job_ids.end()) {
        m_hub->endJob(it->second, tr("Already baked — skipped"));
        m_job_ids.erase(it);
    }
    emit correctionSkipped(lid);
}

void CorrectionBatchOperator::jobFailed(const std::string& lid, const QString& error)
{
    m_overlay->failJob(lid, error);
    const auto it = m_job_ids.find(lid);
    if (it != m_job_ids.end()) {
        m_hub->failJob(it->second, error);
        m_job_ids.erase(it);
    }
    emit correctionFailed(lid, error);
}

} // namespace dolphin::ui
