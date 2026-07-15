#pragma once

#include <QWidget>
#include <QRectF>

namespace dolphin::ui {

// Interactive graph representation of the existing two-coefficient TVG model.
// Handles at 50 m and 200 m solve back to spreading (dB/dec) and absorption
// (dB/m), so Graph and Numeric modes always produce identical processing.
class TvgCurveEditor final : public QWidget {
    Q_OBJECT
public:
    explicit TvgCurveEditor(QWidget* parent = nullptr);
    void setCoefficients(float spreading, float absorption);

signals:
    void coefficientsChanged(float spreading, float absorption);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    QRectF plotRect() const;
    float gainAt(float range_m) const;
    QPointF handlePoint(float range_m) const;
    void updateHandle(const QPointF& position);

    float m_spreading = 20.f;
    float m_absorption = 0.f;
    int   m_drag_handle = 0;
};

} // namespace dolphin::ui
