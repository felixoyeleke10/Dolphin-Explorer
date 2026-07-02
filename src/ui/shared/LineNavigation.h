#pragma once
// LineNavigation — shared "is there a prev/next line?" computation for the viewer
// windows. One helper so the SSS waterfall and SBP seabed viewers compute Prev/Next
// availability identically; each passes a predicate for what counts as a navigable
// line of its modality (sidescanCount() / subBottomCount()).
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"

#include <string>

namespace dolphin::ui {

struct LineNavState {
    bool has_prev = false;
    bool has_next = false;
};

// Returns whether a navigable line exists before/after `current_id` among the
// project layers for which `pred(layer)` is true (in project order).
template <class Pred>
inline LineNavState computeLineNav(const app::Project* project,
                                   const std::string&  current_id,
                                   Pred                pred)
{
    LineNavState st;
    if (!project) return st;
    const auto& layers = project->layers();

    int cur = -1;
    for (int i = 0; i < static_cast<int>(layers.size()); ++i)
        if (layers[i] && layers[i]->id == current_id) { cur = i; break; }
    if (cur < 0) return st;

    for (int i = 0; i < static_cast<int>(layers.size()); ++i) {
        if (i == cur || !layers[i] || !pred(*layers[i])) continue;
        if (i < cur) st.has_prev = true;
        else         st.has_next = true;
    }
    return st;
}

} // namespace dolphin::ui
