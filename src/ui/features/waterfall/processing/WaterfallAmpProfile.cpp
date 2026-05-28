// WaterfallAmpProfile.cpp — amplitude profile computation for the bottom bar.
//
// Separated from the painter so the same profile can be used by future
// consumers (e.g. histogram panels, AGC calibration) without coupling to Qt.

#include "ui/features/waterfall/processing/WaterfallAmpProfile.h"

#include <algorithm>

namespace dolphin::ui {

WaterfallAmpProfile computeAmpProfile(const std::vector<PingRow>&      rows,
                                      const WaterfallScrollController& scroll,
                                      const WfLayout&                  lay)
{
    WaterfallAmpProfile profile;

    if (rows.empty()) return profile;

    const int rh       = lay.row_height;
    const int first    = scroll.scrollRow();
    const int vis_last = std::min(first + lay.img_h / rh,
                                  static_cast<int>(rows.size()));
    if (first >= vis_last) return profile;

    // Determine the common sample count from the visible window.
    int ns = 0;
    for (int r = first; r < vis_last; ++r) {
        ns = std::max(ns, static_cast<int>(rows[r].port.size()));
        ns = std::max(ns, static_cast<int>(rows[r].stbd.size()));
    }
    if (ns == 0) return profile;

    profile.ns = ns;
    profile.port_mean.assign(static_cast<size_t>(ns), 0.f);
    profile.stbd_mean.assign(static_cast<size_t>(ns), 0.f);

    std::vector<int> port_cnt(static_cast<size_t>(ns), 0);
    std::vector<int> stbd_cnt(static_cast<size_t>(ns), 0);

    for (int r = first; r < vis_last; ++r) {
        const PingRow& pr = rows[r];
        for (int i = 0; i < static_cast<int>(pr.port.size()); ++i) {
            profile.port_mean[i] += pr.port[i];
            ++port_cnt[i];
        }
        for (int i = 0; i < static_cast<int>(pr.stbd.size()); ++i) {
            profile.stbd_mean[i] += pr.stbd[i];
            ++stbd_cnt[i];
        }
    }

    for (int i = 0; i < ns; ++i) {
        if (port_cnt[i] > 0) profile.port_mean[i] /= port_cnt[i];
        if (stbd_cnt[i] > 0) profile.stbd_mean[i] /= stbd_cnt[i];
    }

    return profile;
}

} // namespace dolphin::ui
