#pragma once
#include "ui/features/waterfall/PingRow.h"
#include "ui/features/waterfall/rendering/WaterfallRenderer.h"  // WfLayout, coordinate helpers

#include <vector>

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  WaterfallSeabedTracker — drag-edit UI state for the seabed line.
//
//  Handles hit-testing, drag start/apply/end for the manual seabed pen tool.
//  Auto-detection logic has moved to SeabedAutoDetector.
// ─────────────────────────────────────────────────────────────────────────────

class WaterfallSeabedTracker {
public:
    // ── Drag-edit state ────────────────────────────────────────────────────
    bool isDragging() const { return m_drag_row >= 0; }
    int  dragRow()    const { return m_drag_row; }

    // Returns true if the click at (click_x, row_idx) lands within kHitRadius
    // pixels of the seabed line rendered for that row at the given zoom/pan.
    bool hitTest(int click_x, int row_idx,
                 const std::vector<PingRow>& rows,
                 float h_zoom, int h_pan,
                 const WfLayout& layout) const;

    // Start a drag on the given row (call after a successful hitTest).
    void beginDrag(int row_idx);

    // Apply the drag: update rows[m_drag_row].seabed.range_m and set is_manual.
    // Returns true when the update is applied (drag is active and row is valid).
    bool applyDrag(float range_m, std::vector<PingRow>& rows) const;

    // End the drag (call from mouseReleaseEvent).
    void endDrag();

private:
    static constexpr int kHitRadius = 8;  // px tolerance for hit-test

    int m_drag_row = -1;

    // Compute the rendered pixel x for the seabed on starboard (or port when
    // stbd_samples is empty) for the given row, using the same formula as the
    // seabed overlay painter in WaterfallView::paintSeabedOverlay().
    static int seabedPixelStbd(const PingRow& pr, float h_zoom, int h_pan, const WfLayout& layout);
    static int seabedPixelPort(const PingRow& pr, float h_zoom, int h_pan, const WfLayout& layout);
};

} // namespace dolphin::ui
