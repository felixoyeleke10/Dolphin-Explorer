#pragma once

#include "app/corrections/SbpGainParams.h"
#include "app/corrections/SbpSignalParams.h"
#include "core/SubBottomTrace.h"

#include <functional>
#include <vector>

namespace dolphin::app::corrections {

// Applies each requested pass only to traces that do not already carry its
// correction flag. Returns false only when cancellation interrupted the pass.
bool applySubBottomCorrections(std::vector<core::SubBottomTrace>& traces,
                               const SbpGainParams& gain,
                               const SbpSignalParams& signal,
                               const std::function<bool()>& cancelled = {});

} // namespace dolphin::app::corrections
