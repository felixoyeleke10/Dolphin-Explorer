#pragma once
#include <string>
#include <vector>

namespace dolphin::app {

// Metadata for one supported import format.
struct FormatInfo {
    std::string id;           // canonical lower-case ID: "xtf", "jsf", "segy", "dlpd"
    std::string label;        // display name shown in file dialogs
    std::vector<std::string> extensions;  // lower-case, no dot: {"xtf"}, {"sgy","segy"}
    bool supports_sidescan    = false;
    bool supports_subbottom   = false;
    bool supports_magnetometer = false;
    bool supports_multibeam   = false;
};

// Registry of all import formats known to the application.
// Single instance; use FormatRegistry::instance().
class FormatRegistry {
public:
    static const FormatRegistry& instance();

    // Return the FormatInfo for a canonical format ID, or nullptr if unknown.
    const FormatInfo* infoFor(const std::string& format_id) const;

    // Derive a format ID from a file path extension.
    // Returns "" if the extension is not recognised.
    std::string sniffFromPath(const std::string& path) const;

    // Returns true iff the format ID is known to this registry.
    bool isKnown(const std::string& format_id) const;

    // Flat list of all registered formats (for file dialog filter strings).
    const std::vector<FormatInfo>& all() const { return m_formats; }

private:
    FormatRegistry();
    std::vector<FormatInfo> m_formats;
};

} // namespace dolphin::app
