#include "io/ProbeDispatch.h"
#include "io/segy/SegyReader.h"
#include "io/xtf/XtfReader.h"
#include "io/jsf/JsfReader.h"
#include <algorithm>
#include <cctype>

namespace dolphin::io {

namespace {

static std::string lowerExt(const std::string& path)
{
    const auto dot = path.rfind('.');
    if (dot == std::string::npos) return {};
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

} // namespace

ProbeResult probeFile(const std::string& path)
{
    const std::string ext = lowerExt(path);

    if (ext == ".segy" || ext == ".sgy")
        return SegyReader{}.probe(path);
    if (ext == ".xtf")
        return XtfReader{}.probe(path);
    if (ext == ".jsf")
        return JsfReader{}.probe(path);

    ProbeResult r;
    r.path          = path;
    r.format_name   = "Unknown";
    r.error_message = "Unsupported file format (" + ext + ")";
    return r;
}

} // namespace dolphin::io
