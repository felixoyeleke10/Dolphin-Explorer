#pragma once
#include "app/import/FormatRegistry.h"
#include <filesystem>
#include <string>

namespace dolphin::app {

struct PreflightResult {
    bool        ok    = false;
    std::string error;
    explicit operator bool() const noexcept { return ok; }
};

// Validates that a source file is ready to import before any I/O begins.
// Checks: file exists, non-empty, format known to FormatRegistry.
inline PreflightResult checkImportPreflight(const std::string& path,
                                            const std::string& format_id)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(path, ec) || ec)
        return { false, "File does not exist: " + path };

    const auto sz = fs::file_size(path, ec);
    if (ec || sz == 0)
        return { false, "File is empty or unreadable: " + path };

    if (!FormatRegistry::instance().isKnown(format_id))
        return { false, "Unsupported format: " + format_id };

    return { true, {} };
}

} // namespace dolphin::app
