#pragma once

#include "ui/features/waterfall/PingRow.h"
#include "ui/features/waterfall/WaterfallContact.h"

#include <vector>

namespace dolphin::ui {

// Pure contact-candidate analysis. The widget owns persistence, georeferencing,
// signals, and repainting; this service owns only amplitude-pattern policy.
class WaterfallContactDetector {
public:
    static std::vector<WfContact> detect(
        const std::vector<PingRow>& rows,
        const std::vector<WfContact>& existing,
        ContactClass classification,
        int sensitivity,
        int max_candidates = 40);
};

} // namespace dolphin::ui
