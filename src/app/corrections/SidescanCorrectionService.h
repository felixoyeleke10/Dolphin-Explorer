#pragma once
#include <QObject>
#include <string>
#include "app/corrections/SidescanCorrectionParams.h"
#include "core/ArtifactIndex.h"

namespace dolphin::app {
class ImportService;
}

namespace dolphin::app {

// Bakes enabled processing corrections from SidescanCorrectionParams into the
// .dlpd amplitude data in a background thread.
//
// Each applyToLine() call emits exactly one terminal signal:
//   correctionsPersisted — bake succeeded; new amplitudes written to the artifact store
//   applySkipped         — all requested corrections were already baked; nothing written
//   applyFailed          — store unreadable, write failed, or unexpected exception
//
// Already-baked flags are respected — re-applying the same correction is
// idempotent. Batch tracking (pending/succeeded counters) belongs to the caller.
class SidescanCorrectionService : public QObject {
    Q_OBJECT
public:
    explicit SidescanCorrectionService(ImportService* import_service,
                                       QObject* parent = nullptr);

    void applyToLine(const std::string& layer_id,
                     const std::string& store_path,
                     const std::string& store_format,
                     const core::ArtifactIndex& artifact_index,
                     const std::string& source_path,
                     const SidescanCorrectionParams& params);

signals:
    // Emitted when corrections were actually written to the artifact store.
    void correctionsPersisted(const std::string& layer_id,
                              const std::string& new_store_path,
                              const core::ArtifactIndex& new_index);
    // Emitted when all requested corrections were already baked — nothing written.
    void applySkipped(const std::string& layer_id);
    void applyFailed(const std::string& layer_id, const std::string& error);

private:
    ImportService* m_import_service;
};

} // namespace dolphin::app
