#pragma once
#include "io/ProbeResult.h"
#include <string>

namespace dolphin::io {

// Detect format by extension and dispatch to the appropriate reader's probe().
// Fully self-contained: opens and closes the file internally.
// Thread-safe: no shared mutable state — safe to call from QtConcurrent::run.
ProbeResult probeFile(const std::string& path);

} // namespace dolphin::io
