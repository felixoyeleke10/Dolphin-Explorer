#pragma once
#include <QWidget>
#include <vector>

namespace dolphin::ui {

// HistogramRangeSlider — a compact amplitude histogram with two draggable
// handles (black-point / white-point). Used by Views ▸ SSS "Dynamic range"
// to set SonarDisplayParams::display_low / display_high, which are normalised
// amplitude positions in [0,1].
//
// The bars are a display aid; the two handles are the control. rangeChanged
// fires live during a drag (cheap: label/preview updates); rangeCommitted
// fires once on release (the caller triggers the heavier mosaic re-raster
// there so a drag doesn't queue one rebuild per pixel).
class HistogramRangeSlider : public QWidget {
    Q_OBJECT
public:
    explicit HistogramRangeSlider(QWidget* parent = nullptr);

    // Bin heights (any scale — normalised internally). Empty = "no data" hint.
    void setHistogram(std::vector<float> bins);
    // Handle positions in [0,1]; low is clamped below high (min gap enforced).
    void setRange(double low, double high);

    double low()  const { return m_low; }
    double high() const { return m_high; }

signals:
    void rangeChanged(double low, double high);    // live, during drag
    void rangeCommitted(double low, double high);  // once, on mouse release

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    QSize sizeHint() const override;

private:
    enum class Handle { None, Low, High };
    Handle handleAt(int x) const;
    double xToPos(int x) const;
    int    posToX(double pos) const;

    std::vector<float> m_bins;
    float  m_bin_max = 1.f;
    double m_low     = 0.0;
    double m_high    = 1.0;
    Handle m_drag    = Handle::None;

    static constexpr int kPad     = 6;   // left/right inset for handle travel
    static constexpr double kMinGap = 0.02;
};

} // namespace dolphin::ui
