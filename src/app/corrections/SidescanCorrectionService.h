#pragma once
#include <QObject>
#include <string>
#include "app/corrections/SidescanCorrectionParams.h"
#include "core/ArtifactIndex.h"

namespace dolphin::app {
class ImportService;
class Project;
}

namespace dolphin::app {

// Bakes enabled processing corrections from SidescanCorrectionParams into the
// .dlpd amplitude data in a background thread.
//
// Unlike the display-time pipeline, baked corrections are permanent: the
// modified uint16 amplitudes are written back to the artifact store so exports
// and every viewer see the corrected data.
//
// Covered corrections (pre-assembly, operate directly on ping samples):
//   TVG (CorrectionFlag::Tvg)            — time-varying gain
//   ARC (CorrectionFlag::Arc)            — angle range correction
//   AGC (CorrectionFlag::GainNormalized) — gain normalisation
//
// Already-baked flags are respected — re-applying the same correction is
// silently skipped so calling applyToLine twice is idempotent.
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

    void applyToAll(Project& project, const SidescanCorrectionParams& params);

signals:
    void applyStarted(const std::string& layer_id);
    void correctionsPersisted(const std::string& layer_id,
                              const std::string& new_store_path,
                              const core::ArtifactIndex& new_index);
    void applyFailed(const std::string& layer_id, const std::string& error);

private:
    ImportService* m_import_service;
};

} // namespace dolphin::app
