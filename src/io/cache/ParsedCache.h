#pragma once
#include "io/IFormatReader.h"
#include <cstdio>
#include <string>

namespace dolphin::io {

class ParsedCacheReader : public IFormatReader {
public:
    ParsedCacheReader();
    ~ParsedCacheReader() override;

    bool        open(const std::string& path) override;
    void        close() override;
    FormatMeta  metadata() override;

    core::ArtifactIndex buildIndex(ProgressFn progress = {}) override;

    std::optional<core::Artifact>
        readArtifact(const core::ArtifactIndexEntry& entry) override;

    bool        isOpen()     const override { return m_file != nullptr; }
    std::string formatName() const override { return "DPCACHE"; }

private:
    FILE*       m_file = nullptr;
    std::string m_path;
    FormatMeta  m_meta;
    uint64_t    m_fileSize = 0;
};

bool writeParsedCache(const std::string& cache_path,
                      const core::ArtifactIndex& source_index,
                      IFormatReader& source_reader,
                      core::ArtifactIndex& cache_index,
                      ProgressFn progress = {});

// Returns true only if the file exists, has the correct magic, and matches
// the current cache version. Use this to detect stale caches without a full open.
bool parsedCacheIsValid(const std::string& path);

} // namespace dolphin::io
