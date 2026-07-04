#include "ui/features/contacts/ContactSnapshotView.h"
#include "ui/shell/Theme.h"

#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>
#include <algorithm>

namespace dolphin::ui {

ContactSnapshotView::ContactSnapshotView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(320, 320);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

void ContactSnapshotView::setPixmap(const QPixmap& pm)
{
    m_pixmap = pm;
    update();
}

void ContactSnapshotView::setMarkerColor(const QColor& c)
{
    m_marker = c.isValid() ? c : QColor(255, 64, 64);
    update();
}

void ContactSnapshotView::setScalePercent(int pct)
{
    pct = std::clamp(pct, 25, 400);
    if (pct == m_scale_pct) return;
    m_scale_pct = pct;
    update();
    emit scaleChanged(pct);
}

void ContactSnapshotView::setRotationDeg(int deg)
{
    deg = std::clamp(deg, -180, 180);
    if (deg == m_rot_deg) return;
    m_rot_deg = deg;
    update();
    emit rotationChanged(deg);
}

void ContactSnapshotView::setShowMarker(bool on)
{
    if (on == m_show_marker) return;
    m_show_marker = on;
    update();
}

void ContactSnapshotView::resetView()
{
    setScalePercent(100);
    setRotationDeg(0);
}

void ContactSnapshotView::wheelEvent(QWheelEvent* e)
{
    const int notch = e->angleDelta().y();
    if (notch == 0) { e->ignore(); return; }
    setScalePercent(m_scale_pct + (notch > 0 ? 10 : -10));
    e->accept();
}

void ContactSnapshotView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(18, 18, 20));   // dark sonar-viewer backdrop

    if (m_pixmap.isNull()) {
        // Proper empty state: a faint target glyph over a short explanation,
        // instead of a bare string floating in a void.
        const QPointF c(width() / 2.0, height() / 2.0 - 26.0);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen ring(QColor(120, 120, 128, 70), 2.0);
        p.setPen(ring);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, 22.0, 22.0);
        p.drawEllipse(c, 7.0, 7.0);
        p.drawLine(QPointF(c.x(), c.y() - 30), QPointF(c.x(), c.y() - 26));
        p.drawLine(QPointF(c.x(), c.y() + 26), QPointF(c.x(), c.y() + 30));
        p.drawLine(QPointF(c.x() - 30, c.y()), QPointF(c.x() - 26, c.y()));
        p.drawLine(QPointF(c.x() + 26, c.y()), QPointF(c.x() + 30, c.y()));

        QFont f = font();
        f.setPointSizeF(f.pointSizeF() + 1);
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(170, 170, 178));
        QRectF title_r(0, c.y() + 38, width(), 22);
        p.drawText(title_r, Qt::AlignHCenter | Qt::AlignTop, tr("No source image"));

        f.setBold(false);
        f.setPointSizeF(f.pointSizeF() - 1.5);
        p.setFont(f);
        p.setPen(QColor(128, 128, 136));
        QRectF sub_r(width() * 0.12, c.y() + 62, width() * 0.76, 40);
        p.drawText(sub_r, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                   tr("Snapshots are captured when a contact is picked on the "
                      "waterfall. Map- and SBP-picked contacts have no image."));
        return;
    }

    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Fit the pixmap to the widget, then apply the user zoom + rotation about centre.
    const QSizeF avail(width(), height());
    const QSizeF img(m_pixmap.size());
    const double fit = std::min(avail.width() / img.width(),
                                avail.height() / img.height());
    const double s   = fit * (m_scale_pct / 100.0);

    p.translate(width() / 2.0, height() / 2.0);
    p.rotate(m_rot_deg);
    p.scale(s, s);

    const QRectF dst(-img.width() / 2.0, -img.height() / 2.0,
                     img.width(), img.height());
    p.drawPixmap(dst, m_pixmap, QRectF(QPointF(0, 0), img));

    // Target marker at the pick location (snapshot centre). Draw in image space so
    // it tracks zoom/rotation; keep the ring a constant on-screen size.
    if (m_show_marker) {
        p.resetTransform();
        p.translate(width() / 2.0, height() / 2.0);
        const double r = 14.0;
        QPen pen(m_marker, 2.0);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(0, 0), r, r);
        p.drawLine(QPointF(-r - 5, 0), QPointF(-r + 4, 0));
        p.drawLine(QPointF(r - 4, 0),  QPointF(r + 5, 0));
        p.drawLine(QPointF(0, -r - 5), QPointF(0, -r + 4));
        p.drawLine(QPointF(0, r - 4),  QPointF(0, r + 5));
    }
}

} // namespace dolphin::ui
