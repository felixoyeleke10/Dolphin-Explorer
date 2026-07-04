// SubBottomView.cpp — core widget setup and data management.
//
// Paint logic   → SubBottomView.Paint.cpp
// Input/scroll  → SubBottomView.Input.cpp

#include "ui/features/subbottom/SubBottomView.h"
#include <QResizeEvent>
#include <algorithm>

namespace dolphin::ui {

SubBottomView::SubBottomView(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(200, 200);
}

void SubBottomView::setTraces(std::vector<core::SubBottomTrace> traces)
{
    m_traces      = std::move(traces);
    m_first_trace = 0;
    // New line: drop any in-progress feature draft so it can't be finished with
    // vertices from the previous line.
    m_feature_pts.clear();
    m_feature_px.clear();
    m_image_dirty = true;
    update();
    emit scrollChanged(0, traceCount(), visibleTraceCount());
}

void SubBottomView::clear()
{
    m_traces.clear();
    m_first_trace = 0;
    m_feature_tool = 0;
    m_feature_pts.clear();
    m_feature_px.clear();
    m_feature_pen_down = false;
    m_image_dirty = true;
    m_image       = QImage{};
    update();
    emit scrollChanged(0, 0, 0);
}

void SubBottomView::scrollToTrace(int first_trace)
{
    const int max_first = std::max(0, traceCount() - visibleTraceCount());
    m_first_trace = std::clamp(first_trace, 0, max_first);
    m_image_dirty = true;
    update();
}

int SubBottomView::visibleTraceCount() const
{
    return (m_px_per_trace > 0) ? width() / m_px_per_trace : 0;
}

void SubBottomView::setPxPerTrace(int px)
{
    m_px_per_trace = std::max(1, px);
    m_image_dirty  = true;
    update();
    emit scaleChanged(m_px_per_trace, m_px_per_sample);
}

void SubBottomView::setPxPerSample(float px)
{
    m_px_per_sample = std::max(0.05f, px);
    m_image_dirty   = true;
    update();
    emit scaleChanged(m_px_per_trace, m_px_per_sample);
}

void SubBottomView::setPaletteIndex(int idx)
{
    m_palette_index = idx;
    m_image_dirty   = true;
    update();
}

void SubBottomView::setGain(float gain)
{
    m_gain        = std::max(0.01f, gain);
    m_image_dirty = true;
    update();
}

void SubBottomView::setContrast(float contrast)
{
    m_contrast    = std::max(0.1f, contrast);
    m_image_dirty = true;
    update();
}

void SubBottomView::setPolarityInvert(bool invert)
{
    m_polarity_invert = invert;
    m_image_dirty     = true;
    update();
}

void SubBottomView::setShowBottomTrack(bool show)
{
    m_show_bottom_track = show;
    m_image_dirty       = true;
    update();
}

void SubBottomView::setDisplayParams(int palette, float gain, float contrast,
                                      bool polarity_invert, bool show_bottom_track)
{
    m_palette_index     = palette;
    m_gain              = std::max(0.01f, gain);
    m_contrast          = std::max(0.1f, contrast);
    m_polarity_invert   = polarity_invert;
    m_show_bottom_track = show_bottom_track;
    m_image_dirty       = true;
    update();
}

void SubBottomView::resizeEvent(QResizeEvent*)
{
    m_image_dirty = true;
    update();
}

} // namespace dolphin::ui
