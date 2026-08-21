#pragma once
#include <QObject>
#include <string>
#include "app/corrections/SbpGainParams.h"
#include "app/corrections/SbpSignalParams.h"
#include "core/ArtifactIndex.h"

namespace dolphin::app {

// Bakes enabled SBP processing corrections into the .dlpd float sample data
// in a background thread.
//
// Each applyToLine() call emits exactly one terminal signal:
//   correctionsPersisted — bake succeeded; new samples written to the artifact store
//   applySkipped         — all requested corrections were already baked; nothing written
//   applyFailed          — store unreadable, write failed, or unexpected exception
//
// Already-baked flags are respected — re-applying the same correction is
// idempotent. Batch tracking (pending/succeeded counters) belongs to the caller.
class SubBottomCorrectionService : public QObject {
    Q_OBJECT
public:
    explicit SubBottomCorrectionService(QObject* parent = nullptr);

    void applyToLine(const std::string& layer_id,
                     const std::string& store_path,
                     const std::string& store_format,
                     const core::ArtifactIndex& artifact_index,
                     const std::string& source_path,
                     const SbpGainParams& gain,
                     const SbpSignalParams& signal);

signals:
    // Emitted when corrections were actually written to the artifact store.
    void correctionsPersisted(const std::string& layer_id,
                              const std::string& new_store_path,
                              const core::ArtifactIndex& new_index,
                              uint32_t baked_correction_flags);
    // Emitted when all requested corrections were already baked — nothing written.
    void applySkipped(const std::string& layer_id);
    void applyFailed(const std::string& layer_id, const std::string& error);

};

} // namespace dolphin::app
