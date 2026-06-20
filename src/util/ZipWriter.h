#pragma once
// ZipWriter — a minimal, owned ZIP archive writer (STORE method, no compression).
// Enough to build OOXML containers like .docx (Word opens uncompressed zips fine).
// No third-party dependency; the whole archive is buffered then written once, so
// it suits small documents (reports), not large data.
#include <cstdint>
#include <string>
#include <vector>

namespace dolphin::util {

class ZipWriter {
public:
    // Add one stored (uncompressed) entry. `name` is the in-archive path, e.g.
    // "word/document.xml". `data` is the raw bytes (UTF-8 for XML).
    void addFile(const std::string& name, const std::string& data);

    // Finalize the central directory + EOCD and write the archive to `path`.
    // Returns false if the file could not be written.
    bool writeToFile(const std::string& path);

private:
    struct Entry {
        std::string name;
        uint32_t    crc    = 0;
        uint32_t    size   = 0;   // stored == uncompressed (STORE method)
        uint32_t    offset = 0;   // local-header offset within the buffer
    };
    std::string        m_buf;
    std::vector<Entry> m_entries;
};

} // namespace dolphin::util
