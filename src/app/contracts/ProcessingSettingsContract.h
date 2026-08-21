#pragma once

#include "app/corrections/SbpGainParams.h"
#include "app/corrections/SbpSignalParams.h"
#include "app/display/NavProcessingParams.h"
#include "app/display/SubBottomDisplayParams.h"
#include "app/display/WaterfallParams.h"

#include <string>

namespace dolphin::app::contracts {

// Boundary validation shared by Apply, persistence, bake and background views.
// Empty means valid; callers must reject rather than execute invalid settings.
std::string validate(const dolphin::ui::WaterfallParams& params);
std::string validate(const dolphin::ui::SubBottomDisplayParams& params);
std::string validate(const SidescanCorrectionParams& params);
std::string validate(const SbpGainParams& gain, const SbpSignalParams& signal);
std::string validate(const dolphin::ui::NavProcessingParams& params);

} // namespace dolphin::app::contracts
