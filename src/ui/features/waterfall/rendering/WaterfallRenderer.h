#pragma once
#include "ui/features/waterfall/PingRow.h"
#include "ui/features/waterfall/WaterfallParams.h"
#include "render/sonar/SSSPalette.h"
#include "core/SidescanPing.h"

#include <QImage>
#include <vector>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Pixel geometry constants — all values are in LOGICAL pixels.
//
//  HiDPI note: the renderer currently builds a QImage at logical resolution
//  and lets Qt scale it to physical pixels during paint.  To upgrade to a
//  device-pixel-sharp image, rebuild() should:
//    1. Create QImage(phys_w, phys_img_h, Format_RGB32).
//    2. Call img.setDevicePixelRatio(dpr) so Qt reports the correct logical size.
//    3. Scale every coordinate that indexes the image buffer by dpr — i.e.
//       use WfLayout::phys_nadir, phys_ruler_w, and phys_row_h instead of
//       the logical counterparts.
//  The strip cache (per-row pixel buffer) must also be phys_w wide.
//  Hit-testing (xToRange, mouse events, overlay painter) stays in logical pixels.
// -----------------------------------------------------------------------------

inline constexpr int kWfRulerW    = 0;    // no ruler strip — waterfall fills full width
inline constexpr int kWfScaleBarH = 22;   // logical px — top PORT/STBD scale bar
inline constexpr int kWfAmpBarH   = 48;   // logical px — bottom amplitude chart
inline constexpr int kWfLabelPadX = 6;    // logical px — horizontal text padding
inline constexpr int kWfRowHeight = 1;    // logical px per ping row

// -----------------------------------------------------------------------------
//  WfLayout — current widget geometry snapshot.
//  Set by WaterfallView::updateLayout() on every resize and passed into
//  rebuild() / xToRange() / seabed helpers.
//
//  Logical fields are used by hit-testing, overlay painting, and scroll logic.
//  Physical fields (phys_*) are derived from logical * dpr and are intended
//  for the image buffer once the HiDPI upgrade above is implemented.
// -----------------------------------------------------------------------------

struct WfLayout {
    // Logical (Qt-coordinate) fields
    int   widget_w  = 0;              // total widget width
    int   widget_h  = 0;              // total widget height
    int   nadir_x   = 0;             // pixel x of the nadir centre line
    int   img_h     = 0;             // waterfall image height = widget_h - kWfScaleBarH - kWfAmpBarH
    int   row_height = kWfRowHeight;  // logical pixels per ping row (vertical scale)

    // Device pixel ratio — populated from QWidget::devicePixelRatio()
    qreal dpr      = 1.0;

    // Physical (image-buffer) fields — logical * dpr, rounded to nearest int
    int   phys_w        = 0;  // widget_w * dpr
    int   phys_widget_h = 0;  // widget_h * dpr
    int   phys_img_h    = 0;  // img_h    * dpr
    int   phys_nadir    = 0;  // nadir_x  * dpr
    int   phys_ruler_w  = 0;  // kWfRulerW * dpr
    int   phys_row_h    = 0;  // row_height * dpr
};

// -----------------------------------------------------------------------------
//  Inline coordinate helpers
//  Used by both WaterfallRenderer (image building) and
//  WaterfallSeabedTracker (hit-testing) to avoid formula duplication.
// -----------------------------------------------------------------------------

// Sample index → starboard pixel x
inline int sampleToPixelStbd(int si, float z, int h_pan, int nadir_x) noexcept
{
    return nadir_x + static_cast<int>((si - h_pan) * z);
}

// Sample index → port pixel x (mirrored: nadir-1 is sample h_pan)
inline int sampleToPixelPort(int si, float z, int h_pan, int nadir_x) noexcept
{
    return nadir_x - 1 - static_cast<int>((si - h_pan) * z);
}

// -----------------------------------------------------------------------------
//  WaterfallRenderer — builds the sidescan raster image from PingRows.
//
//  One instance is owned by WaterfallView.  All rendering state (colour table,
//  image, strip cache, layout) lives here so WaterfallView's member list stays lean.
// -----------------------------------------------------------------------------

class WaterfallRenderer {
public:
    WaterfallRenderer();

    // Update geometry; call from WaterfallView::resizeEvent() and before the
    // first paint.  Triggers a dirty flag internally.
    void setLayout(const WfLayout& layout);

    // Update display params; rebuilds the display colour table and marks all
    // cached strips stale so only newly-visible rows are re-rendered on the next rebuild().
    void setParams(const WaterfallParams& params);

    // Rebuild m_img from rows[scroll_row … scroll_row+maxVisible()).
    // Uses the per-row strip cache — unchanged rows are blitted without
    // re-rendering.  Returns false when layout dimensions are not yet valid.
    bool rebuild(const std::vector<PingRow>& rows,
                 int   scroll_row,
                 float h_zoom,
                 int   h_pan);

    // Rebuilt image — draw with p.drawImage(0, 0, renderer.image()).
    const QImage& image() const { return m_img; }

    // Rows that fit in img_h at the current row_height.
    int maxVisible() const;

    // Current layout accessor — used by WaterfallView paint subroutines.
    const WfLayout& layout() const { return m_layout; }

    // Widget pixel (x, row_idx) → (channel, range_m).
    // row_idx = scroll_row + widget_y / row_height.
    // Returns false when outside the data area or no rows loaded.
    bool xToRange(int x, int row_idx,
                  const std::vector<PingRow>& rows,
                  float h_zoom, int h_pan,
                  core::SidescanChannel& ch, float& range_m) const;

private:
    void rebuildRow(const PingRow& pr, QRgb* line0,
                    int port_w, int stbd_w, float h_zoom, int h_pan) const;
    void applyRowSmoothing(QRgb* line, int nadir, int canvas_w) const;

    // Rebuild the full uint16 display+palette table from current params and
    // bump m_table_version so all cached strips are considered stale.
    void rebuildColourTable();

    WfLayout         m_layout;
    WaterfallParams  m_params;
    QImage           m_img;

    // CPU fallback: physical uint16 amplitude -> QRgb.
    // Rebuilt whenever any display param or palette changes.
    QRgb     m_colour_table16[65536];

    // Strip pixel cache.  Each entry caches the rendered pixel row for one
    // PingRow at the geometry (widget_w, h_zoom, h_pan) current at render time.
    // table_ver tracks which colour table version the strip was built with;
    // a mismatch forces a re-render without clearing the whole cache vector.
    struct RenderedStrip {
        std::vector<QRgb> pixels;
        uint32_t          table_ver = ~0u;
    };
    std::vector<RenderedStrip> m_strip_cache;
    uint32_t m_table_version = 0;   // bumped on every display param change

    // Geometry keys — when any of these change the strip cache is fully cleared.
    int   m_cache_w    = 0;
    float m_cache_zoom = 0.f;
    int   m_cache_pan  = 0;
};

} // namespace dolphin::ui
