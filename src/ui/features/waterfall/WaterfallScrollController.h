#pragma once
#include <algorithm>    // std::clamp
#include <functional>

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  WaterfallScrollController — vertical scroll, horizontal zoom, and pan state.
//
//  Pure value type — no QObject, no Qt dependency.
//  WaterfallView holds one instance.  All state-change methods return a bool
//  indicating whether the caller should mark the image dirty and repaint.
//
//  The controller communicates scroll-position changes back to WaterfallView
//  via a registered callback (a lambda that emits scrollChanged()).  This keeps
//  the Qt signal origin in WaterfallView (the only QObject here) and makes the
//  controller trivially unit-testable without Qt.
// ─────────────────────────────────────────────────────────────────────────────

class WaterfallScrollController {
public:
    // Callback fired whenever scroll_row changes.
    // Signature: (scroll_row, total_rows, visible_rows)
    using ScrollChangedCb = std::function<void(int, int, int)>;

    void setScrollChangedCallback(ScrollChangedCb cb)
    {
        m_on_scroll_changed = std::move(cb);
    }

    // ── Accessors ──────────────────────────────────────────────────────────
    int   scrollRow() const { return m_scroll_row; }
    float hZoom()     const { return m_h_zoom; }
    int   hPan()      const { return m_h_pan; }
    bool  wantEnd()   const { return m_want_end; }

    // ── Vertical scroll ────────────────────────────────────────────────────

    // Jump to a specific row.  Returns true if the scroll position changed
    // (the caller should repaint).
    bool scrollToRow(int row, int total_rows, int visible_rows);

    // Request deferred scroll-to-end: resolved in resolveWantEnd() on first
    // paint when real dimensions are known.
    void scrollToEnd();

    // Called from rebuildImage() once real dimensions are available.
    // Resolves m_want_end and fires the callback.
    // Returns true if the scroll row was actually updated.
    bool resolveWantEnd(int total_rows, int visible_rows);

    // Re-clamp after a resize (call from resizeEvent).
    // Returns true if the scroll row was adjusted.
    bool clampToMax(int total_rows, int visible_rows);

    // Force-fire the scrollChanged callback with the current row and the given
    // total/visible counts.  Used to re-sync the external scrollbar after the
    // widget layout is established (maxVisible changes from its default of 1).
    void resync(int total_rows, int visible_rows);

    // ── Vertical wheel (plain scroll) ──────────────────────────────────────

    enum class ScrollResult { Ok, AtTop, AtBottom };

    // Process a vertical wheel delta (angle / 40, negative = down).
    // Returns AtTop / AtBottom when the user scrolls past the data boundary
    // so WaterfallView can emit scrollBeyondBounds(-1 / +1).
    ScrollResult scrollBy(int delta, int total_rows, int visible_rows);

    // ── Horizontal zoom / pan ──────────────────────────────────────────────

    // Ctrl+scroll: factor > 1 = zoom in, factor < 1 = zoom out.
    // auto_zoom = currently computed auto-fit pixels-per-sample.
    // Returns true if zoom changed.
    bool zoomBy(float factor, float auto_zoom);

    // Shift+scroll: shift in samples.
    // Returns true if pan changed.
    bool panBy(int delta_samples, int max_samples);

    // Set h-zoom directly (called from explicit scale control).
    // Pass 0 to restore auto-fit.
    bool setHZoom(float zoom);

    // Reset zoom and pan to auto-fit (called when loading a new file).
    void resetZoomPan();

private:
    int   m_scroll_row = 0;
    bool  m_want_end   = false;
    float m_h_zoom     = 0.f;   // 0 = auto-fit; >0 = pixels per sample
    int   m_h_pan      = 0;     // sample offset from nadir

    ScrollChangedCb m_on_scroll_changed;

    void fireScrollChanged(int total_rows, int visible_rows);
};

// ─────────────────────────────────────────────────────────────────────────────
//  Inline implementations (all trivially small)
// ─────────────────────────────────────────────────────────────────────────────

inline void WaterfallScrollController::scrollToEnd()
{
    m_want_end = true;
}

inline bool WaterfallScrollController::setHZoom(float zoom)
{
    if (zoom == m_h_zoom) return false;
    m_h_zoom = zoom;
    if (zoom == 0.f) m_h_pan = 0;
    return true;
}

inline void WaterfallScrollController::resetZoomPan()
{
    m_h_zoom = 0.f;
    m_h_pan  = 0;
}

inline void WaterfallScrollController::fireScrollChanged(int total, int visible)
{
    if (m_on_scroll_changed)
        m_on_scroll_changed(m_scroll_row, total, visible);
}

inline bool WaterfallScrollController::scrollToRow(int row, int total, int visible)
{
    const int max     = std::max(0, total - visible);
    const int clamped = std::clamp(row, 0, max);
    if (clamped == m_scroll_row && !m_want_end) return false;
    m_want_end   = false;
    m_scroll_row = clamped;
    fireScrollChanged(total, visible);
    return true;
}

inline bool WaterfallScrollController::resolveWantEnd(int total, int visible)
{
    if (!m_want_end) return false;
    m_scroll_row = std::max(0, total - visible);
    m_want_end   = false;
    fireScrollChanged(total, visible);
    return true;
}

inline void WaterfallScrollController::resync(int total, int visible)
{
    fireScrollChanged(total, visible);
}

inline bool WaterfallScrollController::clampToMax(int total, int visible)
{
    const int max = std::max(0, total - visible);
    if (m_scroll_row <= max) return false;
    m_scroll_row = max;
    fireScrollChanged(total, visible);
    return true;
}

inline WaterfallScrollController::ScrollResult
WaterfallScrollController::scrollBy(int delta, int total, int visible)
{
    const int max       = std::max(0, total - visible);
    const int requested = m_scroll_row + delta;

    if (requested < 0 && m_scroll_row == 0 && delta < 0)
        return ScrollResult::AtTop;
    if (requested > max && m_scroll_row == max && delta > 0)
        return ScrollResult::AtBottom;

    scrollToRow(requested, total, visible);
    return ScrollResult::Ok;
}

inline bool WaterfallScrollController::zoomBy(float factor, float auto_zoom)
{
    const float current_z = (m_h_zoom > 0.f) ? m_h_zoom : auto_zoom;
    const float new_z     = current_z * factor;

    if (factor < 1.f && new_z <= auto_zoom) {
        if (m_h_zoom == 0.f && m_h_pan == 0) return false;
        m_h_zoom = 0.f;
        m_h_pan  = 0;
    } else {
        const float clamped = std::clamp(new_z, auto_zoom * 0.5f, 20.f);
        if (clamped == m_h_zoom) return false;
        m_h_zoom = clamped;
    }
    return true;
}

inline bool WaterfallScrollController::panBy(int delta_samples, int max_samples)
{
    const int new_pan = std::clamp(m_h_pan + delta_samples, 0, std::max(0, max_samples));
    if (new_pan == m_h_pan) return false;
    m_h_pan = new_pan;
    return true;
}

} // namespace dolphin::ui
