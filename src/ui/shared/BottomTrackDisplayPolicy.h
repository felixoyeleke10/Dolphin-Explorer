#pragma once

namespace dolphin::ui {

// Bottom picks are processing/QC inputs. Once slant-range correction is being
// presented, the normal display must not turn those picks into an output line.
// An active editing tool is the sole exception: the operator must be able to
// see the control they are adjusting.
inline bool shouldPaintBottomTrack(bool requested,
                                   bool slant_range_corrected,
                                   bool editing)
{
    return editing || (requested && !slant_range_corrected);
}

} // namespace dolphin::ui
