#pragma once
#include <QColor>
#include <QPixmap>
#include <QPointF>
#include <QWidget>
#include <array>

namespace dolphin::ui {

// ContactSnapshotView — the source-image pane of the Contact Editor.
//
// Displays the contact's persisted snapshot (a square crop around the pick),
// centred and fitted, with interactive zoom and rotation and an optional
// target marker drawn at the pick location (the snapshot centre). This mirrors
// the SonarWiz / SeaView "source image" viewer: the operator can zoom and rotate
// to inspect the target, and toggle the pick icon on or off.
class ContactSnapshotView : public QWidget {
    Q_OBJECT
public:
    enum MeasurementMode {
        NoMeasurement = 0, MeasureLength, MeasureWidth, MeasureHeight, MeasureShadow
    };
    Q_ENUM(MeasurementMode)
    explicit ContactSnapshotView(QWidget* parent = nullptr);

    void setPixmap(const QPixmap& pm);   // empty → "no source image" placeholder
    bool hasPixmap() const { return !m_pixmap.isNull(); }

    void setMarkerColor(const QColor& c);   // pick-marker tint (per-contact colour)
    void setMeasurementScale(float across_m_per_px, float along_m_per_px);
    void setContactSide(int side); // -1 port, +1 starboard, 0 unknown
    bool hasMeasurementScale() const;
    void setMeasurementMode(MeasurementMode mode);
    void clearMeasurements();

public slots:
    void setScalePercent(int pct);       // 25 … 400
    void setRotationDeg(int deg);        // -180 … 180
    void setShowMarker(bool on);         // draw the pick target icon
    void resetView();                    // scale 100 %, rotation 0

signals:
    void scaleChanged(int pct);
    void rotationChanged(int deg);
    void measurementCompleted(int mode, double metres);

protected:
    void paintEvent(QPaintEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QPixmap m_pixmap;
    int     m_scale_pct = 100;
    int     m_rot_deg   = 0;
    bool    m_show_marker = true;
    QColor  m_marker{ 255, 64, 64 };   // default target-red
    float m_across_m_per_px = 0.f;
    float m_along_m_per_px  = 0.f;
    int m_contact_side = 0;
    MeasurementMode m_measure_mode = NoMeasurement;
    bool m_measuring = false;
    QPointF m_measure_start;
    QPointF m_measure_end;
    struct MeasurementLine { bool valid = false; QPointF a; QPointF b; };
    std::array<MeasurementLine, 4> m_measurements;

    QPointF widgetToImage(const QPointF& pos) const;
    QPointF constrainedEnd(const QPointF& pos) const;
};

} // namespace dolphin::ui
