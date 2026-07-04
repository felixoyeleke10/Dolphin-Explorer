#pragma once
#include <QColor>
#include <QPixmap>
#include <QWidget>

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
    explicit ContactSnapshotView(QWidget* parent = nullptr);

    void setPixmap(const QPixmap& pm);   // empty → "no source image" placeholder
    bool hasPixmap() const { return !m_pixmap.isNull(); }

    void setMarkerColor(const QColor& c);   // pick-marker tint (per-contact colour)

public slots:
    void setScalePercent(int pct);       // 25 … 400
    void setRotationDeg(int deg);        // -180 … 180
    void setShowMarker(bool on);         // draw the pick target icon
    void resetView();                    // scale 100 %, rotation 0

signals:
    void scaleChanged(int pct);
    void rotationChanged(int deg);

protected:
    void paintEvent(QPaintEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    QPixmap m_pixmap;
    int     m_scale_pct = 100;
    int     m_rot_deg   = 0;
    bool    m_show_marker = true;
    QColor  m_marker{ 255, 64, 64 };   // default target-red
};

} // namespace dolphin::ui
