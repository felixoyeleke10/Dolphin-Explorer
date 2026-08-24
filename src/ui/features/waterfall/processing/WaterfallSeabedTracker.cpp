// WaterfallSeabedTracker.cpp — drag-edit UI state for the seabed line
//
// Auto-detection logic has moved to SeabedAutoDetector.

#include "ui/features/waterfall/processing/WaterfallSeabedTracker.h"
#include "ui/features/waterfall/rendering/WaterfallRangeGeometry.h"

#include <QtGlobal>   // qBound
#include <cstdlib>    // std::abs

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Drag state management
// -----------------------------------------------------------------------------

void WaterfallSeabedTracker::beginDrag(int row_idx)
{
    m_drag_row = row_idx;
}

bool WaterfallSeabedTracker::applyDrag(float range_m, std::vector<PingRow>& rows) const
{
    if (m_drag_row < 0 || m_drag_row >= static_cast<int>(rows.size())) return false;
    if (range_m <= 0.f) return false;
    auto& seabed      = rows[m_drag_row].seabed;
    seabed.range_m    = range_m;
    seabed.detected   = true;
    seabed.is_manual  = true;
    return true;
}

void WaterfallSeabedTracker::endDrag()
{
    m_drag_row = -1;
}

// -----------------------------------------------------------------------------
//  Hit-test helpers
// -----------------------------------------------------------------------------

// Compute the rendered pixel x for the seabed return on the starboard side.
// Returns INT_MIN when the row has no starboard data or is out of the viewport.
int WaterfallSeabedTracker::seabedPixelStbd(const PingRow& pr, float h_zoom, int h_pan,
                                             const WfLayout& layout, bool src_enabled)
{
    const int ns = static_cast<int>(pr.stbd.size());
    const auto& pick = waterfallSeabedForSide(
        pr, core::SidescanChannel::Starboard);
    if (ns <= 1 || pick.range_m <= 0.f)
        return INT_MIN;

    const int stbd_w = layout.widget_w - layout.nadir_x;
    const float z    = (h_zoom > 0.f) ? h_zoom
                     : (stbd_w > 0 ? float(stbd_w) / ns : 1.f);
    const float distance = waterfallPixelDistanceForRange(
        pr, core::SidescanChannel::Starboard,
        {pick.range_m, waterfallSeabedDomainForSide(
            pr, core::SidescanChannel::Starboard)}, src_enabled, h_pan, z);
    return distance >= 0.f
        ? layout.nadir_x + static_cast<int>(std::lround(distance)) : INT_MIN;
}

int WaterfallSeabedTracker::seabedPixelPort(const PingRow& pr, float h_zoom, int h_pan,
                                             const WfLayout& layout, bool src_enabled)
{
    const int ns = static_cast<int>(pr.port.size());
    const auto& pick = waterfallSeabedForSide(
        pr, core::SidescanChannel::Port);
    if (ns <= 1 || pick.range_m <= 0.f)
        return INT_MIN;

    const int port_w = layout.nadir_x - kWfRulerW;
    const float z    = (h_zoom > 0.f) ? h_zoom
                     : (port_w > 0 ? float(port_w) / ns : 1.f);
    const float distance = waterfallPixelDistanceForRange(
        pr, core::SidescanChannel::Port,
        {pick.range_m, waterfallSeabedDomainForSide(
            pr, core::SidescanChannel::Port)}, src_enabled, h_pan, z);
    return distance >= 0.f
        ? layout.nadir_x - 1 - static_cast<int>(std::lround(distance)) : INT_MIN;
}

bool WaterfallSeabedTracker::hitTest(int click_x, int row_idx,
                                      const std::vector<PingRow>& rows,
                                      float h_zoom, int h_pan,
                                      const WfLayout& layout,
                                      bool src_enabled) const
{
    if (row_idx < 0 || row_idx >= static_cast<int>(rows.size())) return false;
    const PingRow& pr = rows[row_idx];

    const int xs = seabedPixelStbd(pr, h_zoom, h_pan, layout, src_enabled);
    if (xs != INT_MIN && std::abs(click_x - xs) <= kHitRadius) return true;

    const int xp = seabedPixelPort(pr, h_zoom, h_pan, layout, src_enabled);
    if (xp != INT_MIN && std::abs(click_x - xp) <= kHitRadius) return true;

    return false;
}

} // namespace dolphin::ui
