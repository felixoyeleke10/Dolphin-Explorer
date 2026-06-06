#pragma once
// Canonical definition lives in the app layer — include it and alias into dolphin::ui
// so existing UI code continues to work without changes.
#include "app/corrections/SbpGainParams.h"
namespace dolphin::ui { using SbpGainParams = dolphin::app::SbpGainParams; }
