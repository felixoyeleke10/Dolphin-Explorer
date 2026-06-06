#pragma once
#include <QObject>
#include <string>
#include "app/corrections/SbpGainParams.h"
#include "app/corrections/SbpSignalParams.h"
#include "core/ArtifactIndex.h"

namespace dolphin::app {
class ImportService;
class Project;
}

namespace dolphin::app {

// Bakes enabled SBP processing corrections into the .dlpd float sample data
// in a background thread.
//
// Covered corrections:
//   DcRemoval  — per-trace mean subtraction
//   Envelope   — instantaneous amplitude
//   Normalize  — per-trace peak normalisation
//   StaticGain — constant dB gain
//   Agc        — sliding-window RMS normalisation
//
// Already-baked flags are respected — re-applying the same correction is
// silently skipped so calling applyToLine twice is idempotent.
class SubBottomCorrectionService : public QObject {
    Q_OBJECT
public:
    explicit SubBottomCorrectionService(ImportService* import_service,
                                        QObject* parent = nullptr);

    void applyToLine(const std::string& layer_id,
                     const std::string& store_path,
                     const std::string& store_format,
                     const core::ArtifactIndex& artifact_index,
                     const std::string& source_path,
                     const SbpGainParams& gain,
                     const SbpSignalParams& signal);

    void applyToAll(Project& project,
                    const SbpGainParams& gain,
                    const SbpSignalParams& signal);

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
