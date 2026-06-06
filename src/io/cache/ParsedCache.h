#pragma once
#include "io/IFormatReader.h"
#include "core/Artifact.h"
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

namespace dolphin::io {

class ParsedCacheReader : public IFormatReader {
public:
    ParsedCacheReader();
    ~ParsedCacheReader() override;

    bool        open(const std::string& path) override;
    void        close() override;
    FormatMeta  metadata() override;

    core::ArtifactIndex buildIndex(ProgressFn progress = {}) override;

    // Cancellable overload — cancel_flag is checked every 256 records.
    // Returns an empty index (as if the file were empty) when cancelled.
    // Accepts a raw atomic flag so io/ does not depend on app/tasks/.
    core::ArtifactIndex buildIndex(ProgressFn progress,
                                   const std::atomic<bool>* cancel_flag);

    std::optional<core::Artifact>
        readArtifact(const core::ArtifactIndexEntry& entry) override;

    bool        isOpen()     const override { return m_file != nullptr; }
    std::string formatName() const override { return "DPCACHE"; }

private:
    FILE*       m_file         = nullptr;
    std::string m_path;
    FormatMeta  m_meta;
    uint64_t    m_fileSize     = 0;
    uint32_t    m_file_version = 0;
    uint64_t    m_cur_pos      = UINT64_MAX; // tracked to skip redundant seeks on sequential reads
};

bool writeParsedCache(const std::string& cache_path,
                      const core::ArtifactIndex& source_index,
                      IFormatReader& source_reader,
                      core::ArtifactIndex& cache_index,
                      ProgressFn progress = {});

// Write a processed artifact buffer directly to a DLPD cache file.
// Used by ProcessingService to persist graph-pipeline output without going
// through a reader.  On success populates out_index and returns true.
bool writeArtifactBufferToCache(const std::string& cache_path,
                                const std::vector<core::Artifact>& buffer,
                                const FormatMeta& meta,
                                core::ArtifactIndex& out_index);

// Returns true only if the file exists, has the correct magic, and matches
// the current cache version. Use this to detect stale caches without a full open.
bool parsedCacheIsValid(const std::string& path);

} // namespace dolphin::io
