#pragma once
#include "ui/shell/Theme.h"
#include "ui/features/waterfall/PingRow.h"
#include "ui/features/waterfall/processing/WaterfallAmpProfile.h"
#include "ui/features/waterfall/WaterfallContact.h"
#include "ui/features/waterfall/rendering/WaterfallRenderer.h"         // WfLayout, kWf* constants
#include "ui/features/waterfall/WaterfallScrollController.h"
#include "ui/features/waterfall/WaterfallOverlayParams.h"

#include <QColor>
#include <QRect>
#include <vector>

class QPainter;

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  WaterfallOverlayPainter — stateless overlay drawing for WaterfallView.
//
//  All methods are static.  WaterfallView calls these from its paintEvent()
//  after drawing the raw raster image from WaterfallRenderer.
//
//  Overlays (drawn in order):
//    paintSeabedOverlay  — cyan polyline tracking bottom-detection
//    paintContactOverlay — shared project symbols/colors for placed contacts
//    paintScaleBar       — two-section port/starboard bar at the top edge
//    paintAmplitudeBar   — per-column mean amplitude chart at the bottom edge
//    paintRuler          — ping-index labels on the left depth ruler
//    paintCrosshair      — semi-transparent crosshair under the cursor
//    paintEmptyState     — centred placeholder when no data is loaded
// -----------------------------------------------------------------------------

class WaterfallOverlayPainter {
public:
    static void paintSeabedOverlay (QPainter&                          p,
                                    const std::vector<PingRow>&        rows,
                                    const WfLayout&                    lay,
                                    const WaterfallScrollController&   scroll);

    static void paintContactOverlay(QPainter&                          p,
                                    const std::vector<WfContact>&      contacts,
                                    const std::vector<PingRow>&        rows,
                                    const WfLayout&                    lay,
                                    const WaterfallScrollController&   scroll,
                                    QColor fill = QColor(Theme::kWarning));

    // On-screen pixel position of a contact marker (same math the painter uses).
    // Returns false when the contact is outside the visible rows / channel width.
    // Shared with WaterfallView's marker hit-testing (double-click → editor).
    static bool contactPixelPos    (const WfContact&                   c,
                                    const std::vector<PingRow>&        rows,
                                    const WfLayout&                    lay,
                                    const WaterfallScrollController&   scroll,
                                    QPoint&                            out_px);

    static void paintScaleBar      (QPainter&                          p,
                                    const std::vector<PingRow>&        rows,
                                    const WfLayout&                    lay,
                                    const WaterfallScrollController&   scroll,
                                    int                                samples_per_ping);

    static void paintRuler         (QPainter&                          p,
                                    const WfLayout&                    lay,
                                    const WaterfallScrollController&   scroll,
                                    int                                row_count);

    static void paintCrosshair     (QPainter&                          p,
                                    const WfLayout&                    lay,
                                    int                                cursor_x,
                                    int                                cursor_y,
                                    const WfOverlayParams&             overlay);

    static void paintRangeGrid     (QPainter&                          p,
                                    const std::vector<PingRow>&        rows,
                                    const WfLayout&                    lay,
                                    const WaterfallScrollController&   scroll,
                                    const WfOverlayParams&             overlay);

    static void paintAmplitudeBar  (QPainter&                          p,
                                    const WaterfallAmpProfile&         profile,
                                    const WfLayout&                    lay,
                                    const WaterfallScrollController&   scroll);

    static void paintEmptyState    (QPainter&                          p,
                                    const QRect&                       widget_rect,
                                    bool                               empty);
};

} // namespace dolphin::ui
