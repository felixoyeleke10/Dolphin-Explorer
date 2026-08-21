#include "ui/shared/contacts/ContactSymbols.h"

#include <QPainter>
#include <QPixmap>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace dolphin::ui {

namespace {
constexpr double kPi = 3.14159265358979323846;
} // namespace

const ContactSymbolOpt kContactSymbols[] = {
    { QT_TRANSLATE_NOOP("ContactSymbols", "Auto (Diamond)"), ""         },
    { QT_TRANSLATE_NOOP("ContactSymbols", "Diamond"),        "diamond"  },
    { QT_TRANSLATE_NOOP("ContactSymbols", "Circle"),         "circle"   },
    { QT_TRANSLATE_NOOP("ContactSymbols", "Square"),         "square"   },
    { QT_TRANSLATE_NOOP("ContactSymbols", "Triangle"),       "triangle" },
    { QT_TRANSLATE_NOOP("ContactSymbols", "Cross"),          "cross"    },
    { QT_TRANSLATE_NOOP("ContactSymbols", "Star"),           "star"     },
    { QT_TRANSLATE_NOOP("ContactSymbols", "Pin"),             "pin"     },
    { QT_TRANSLATE_NOOP("ContactSymbols", "Navigation Arrow"), "nav"    },
};
const int kContactSymbolCount =
    static_cast<int>(sizeof(kContactSymbols) / sizeof(kContactSymbols[0]));

QPainterPath contactSymbolPath(const QString& symbol_id, qreal r)
{
    QPainterPath path;

    if (symbol_id == QLatin1String("circle")) {
        path.addEllipse(QPointF(0, 0), r, r);
        return path;
    }
    if (symbol_id == QLatin1String("square")) {
        path.addRect(QRectF(-r, -r, r * 2.0, r * 2.0));
        return path;
    }
    if (symbol_id == QLatin1String("triangle")) {
        QPolygonF tri;
        tri << QPointF(0, -r) << QPointF(r * 0.866, r * 0.5)
            << QPointF(-r * 0.866, r * 0.5);
        path.addPolygon(tri);
        path.closeSubpath();
        return path;
    }
    if (symbol_id == QLatin1String("cross")) {
        const qreal w = r * 0.36;
        QPolygonF plus;
        plus << QPointF(-w, -r) << QPointF(w, -r) << QPointF(w, -w) << QPointF(r, -w)
             << QPointF(r, w)   << QPointF(w, w)   << QPointF(w, r)  << QPointF(-w, r)
             << QPointF(-w, w)  << QPointF(-r, w)  << QPointF(-r, -w) << QPointF(-w, -w);
        path.addPolygon(plus);
        path.closeSubpath();
        return path;
    }
    if (symbol_id == QLatin1String("star")) {
        QPolygonF star;
        for (int i = 0; i < 10; ++i) {
            const double angle = -kPi / 2.0 + i * kPi / 5.0;
            const qreal rad = (i % 2 == 0) ? r : r * 0.45;
            star << QPointF(rad * std::cos(angle), rad * std::sin(angle));
        }
        path.addPolygon(star);
        path.closeSubpath();
        return path;
    }
    if (symbol_id == QLatin1String("pin")) {
        // Balloon-pin silhouette, same shape family as makeContactThumb's marker.
        // Unlike the symmetric shapes above, the pin's *tip* — not its centroid —
        // is the local origin, so it lands exactly on the contact's geo position
        // (the universal map-pin convention: the point touches the ground).
        const QPointF head_ctr(0, -r * 2.15);
        const qreal head_r = r * 0.85;
        QPainterPath head;
        head.addEllipse(head_ctr, head_r, head_r);
        QPolygonF tip_poly;
        tip_poly << QPointF(0, 0)
                 << QPointF(-head_r * 0.72, head_ctr.y() + head_r * 0.55)
                 << QPointF(head_r * 0.72, head_ctr.y() + head_r * 0.55);
        QPainterPath tip;
        tip.addPolygon(tip_poly);
        tip.closeSubpath();
        return head.united(tip);
    }
    if (symbol_id == QLatin1String("nav")) {
        // Location-arrow / compass-needle kite: a concave notch at the back
        // distinguishes it from the plain triangle at a glance.
        QPolygonF kite;
        kite << QPointF(0, -r * 1.05) << QPointF(r * 0.62, r * 0.65)
             << QPointF(0, r * 0.25) << QPointF(-r * 0.62, r * 0.65);
        path.addPolygon(kite);
        path.closeSubpath();
        return path;
    }

    // "" (Auto), "diamond", and any unrecognized/legacy id: the historical diamond.
    QPolygonF diamond;
    diamond << QPointF(0, -r) << QPointF(r, 0) << QPointF(0, r) << QPointF(-r, 0);
    path.addPolygon(diamond);
    path.closeSubpath();
    return path;
}

QIcon contactSymbolIcon(const QString& symbol_id, int px,
                        const QColor& fill, const QColor& outline)
{
    QPixmap pixmap(px, px);
    pixmap.fill(Qt::transparent);

    // Build at a generous fixed radius, then fit-and-centre the resulting
    // bounding box into the icon — shapes like the pin aren't centred on their
    // own bounding box, so a blind translate-to-centre would clip or misplace
    // them. Padding keeps strokes from touching the icon's edge.
    const QPainterPath path = contactSymbolPath(symbol_id, 100.0);
    const QRectF bounds = path.boundingRect();

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    if (!bounds.isEmpty()) {
        constexpr qreal kPaddingFrac = 0.14;
        const qreal pad = px * kPaddingFrac;
        const qreal avail = px - 2.0 * pad;
        const qreal scale = avail / std::max(bounds.width(), bounds.height());
        painter.translate(px / 2.0, px / 2.0);
        painter.scale(scale, scale);
        painter.translate(-bounds.center());
    }
    painter.setPen(QPen(outline, 1.0 / std::max(0.01, painter.transform().m11())));
    painter.setBrush(fill);
    painter.drawPath(path);
    return QIcon(pixmap);
}

} // namespace dolphin::ui
