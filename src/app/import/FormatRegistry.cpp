#include "app/import/FormatRegistry.h"
#include <cctype>

namespace dolphin::app {

FormatRegistry::FormatRegistry()
{
    m_formats = {
        { "xtf",  "eXtended Triton Format",  {"xtf"},             true,  true,  false, false },
        { "jsf",  "EdgeTech JSF",             {"jsf"},             true,  true,  false, false },
        { "segy", "SEG-Y",                    {"segy", "sgy"},     false, true,  false, false },
        { "dlpd", "Dolphin Project Data",     {"dlpd", "dpcache"}, true,  true,  true,  true  },
    };
}

const FormatRegistry& FormatRegistry::instance()
{
    static const FormatRegistry reg;
    return reg;
}

const FormatInfo* FormatRegistry::infoFor(const std::string& format_id) const
{
    for (const auto& f : m_formats)
        if (f.id == format_id) return &f;
    return nullptr;
}

std::string FormatRegistry::sniffFromPath(const std::string& path) const
{
    const auto dot = path.rfind('.');
    if (dot == std::string::npos) return {};

    std::string ext = path.substr(dot + 1);
    for (auto& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    for (const auto& f : m_formats)
        for (const auto& e : f.extensions)
            if (e == ext) return f.id;
    return {};
}

bool FormatRegistry::isKnown(const std::string& format_id) const
{
    if (infoFor(format_id) != nullptr) return true;
    // Also accept extension aliases (e.g. "sgy" for segy, "dpcache" for dlpd).
    for (const auto& f : m_formats)
        for (const auto& e : f.extensions)
            if (e == format_id) return true;
    return false;
}

} // namespace dolphin::app
