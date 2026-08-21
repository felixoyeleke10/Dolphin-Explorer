#include "ui/features/contacts/ContactVisuals.h"
#include "ui/shell/Theme.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"

#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace dolphin::ui::cmvis {

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

bool isFavourite(const core::Contact& c)
{
    return std::find(c.tags.begin(), c.tags.end(), kFavTag) != c.tags.end();
}

QString modalityTag(app::Modality m)
{
    switch (m) {
    case app::Modality::Sidescan:     return QStringLiteral("SSS");
    case app::Modality::SubBottom:    return QStringLiteral("SBP");
    case app::Modality::Magnetometer: return QStringLiteral("MAG");
    case app::Modality::Multibeam:    return QStringLiteral("MBES");
    case app::Modality::Raster:       return QStringLiteral("RAS");
    default:                          return QStringLiteral("—");
    }
}

QString confidenceLabel(core::Confidence c)
{
    switch (c) {
    case core::Confidence::Probable: return QObject::tr("Probable");
    case core::Confidence::Certain:  return QObject::tr("Certain");
    case core::Confidence::Possible:
    default:                         return QObject::tr("Possible");
    }
}

QColor sensorColor(const QString& tag)
{
    if (tag == QLatin1String("SSS"))  return QColor(Theme::kNodeColorFilter);
    if (tag == QLatin1String("SBP"))  return QColor(Theme::kNodeColorMerge);
    if (tag == QLatin1String("MAG"))  return QColor(Theme::kNodeColorAnalysis);
    if (tag == QLatin1String("MBES")) return QColor(Theme::kNodeColorEnhancement);
    if (tag == QLatin1String("RAS"))  return QColor(Theme::kNodeColorUnknown);
    return Theme::textMutedColor();
}

QColor confidenceColor(const QString& label)
{
    if (label == QObject::tr("Certain"))  return QColor(Theme::kSuccess);
    if (label == QObject::tr("Probable")) return QColor(Theme::kCaution);
    return Theme::textSubtleColor();
}

QString bucketKey(int facet, const core::Contact& c, app::Project* proj)
{
    (void)proj;   // group bucketing is handled separately (NavGroup)
    switch (facet) {
    case GroupClass:
        return c.classification.empty() ? QObject::tr("Unclassified")
                                        : QString::fromStdString(c.classification);
    case GroupConfidence:
        return confidenceLabel(c.confidence);
    case GroupDate:
        return c.created_at > 0.0
            ? QDateTime::fromSecsSinceEpoch(static_cast<qint64>(c.created_at)).toString("yyyy-MM-dd")
            : QObject::tr("Unknown date");
    case GroupLabel: {
        const QString l = QString::fromStdString(c.label).trimmed();
        if (l.isEmpty()) return QStringLiteral("#");
        const QChar ch = l.at(0).toUpper();
        return ch.isLetter() ? QString(ch) : QStringLiteral("#");
    }
    default:
        return QString();
    }
}

int confidenceRank(const QString& label)
{
    if (label == QObject::tr("Certain"))  return 0;
    if (label == QObject::tr("Probable")) return 1;
    return 2;   // Possible
}

QString sensorFolderLabel(app::Modality m)
{
    switch (m) {
    case app::Modality::Sidescan:     return QObject::tr("Sidescan (SSS)");
    case app::Modality::SubBottom:    return QObject::tr("Sub-bottom (SBP)");
    case app::Modality::Magnetometer: return QObject::tr("Magnetometer (MAG)");
    case app::Modality::Multibeam:    return QObject::tr("Multibeam (MBES)");
    case app::Modality::Raster:       return QObject::tr("Raster");
    default:                          return QObject::tr("Other");
    }
}

void ChipDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const
{
    p->save();
    if (opt.state & QStyle::State_Selected)
        p->fillRect(opt.rect, QColor(10, 132, 255, 76));

    const QString text = idx.data(Qt::DisplayRole).toString();
    if (text.isEmpty() || text == QStringLiteral("—")) {
        p->restore();
        QStyledItemDelegate::paint(p, opt, idx);
        return;
    }

    const QColor c = (idx.column() == ColSensor) ? sensorColor(text)
                                                 : confidenceColor(text);
    p->setRenderHint(QPainter::Antialiasing, true);

    QFont f = opt.font;
    f.setPointSizeF(f.pointSizeF() - 0.5);
    f.setBold(true);
    const QFontMetrics fm(f);
    const int tw = fm.horizontalAdvance(text);
    const int h  = 17;
    QRect chip(opt.rect.left() + 8, opt.rect.center().y() - h / 2 + 1, tw + 18, h);

    QColor fill = c; fill.setAlpha(38);
    p->setPen(Qt::NoPen);
    p->setBrush(fill);
    p->drawRoundedRect(chip, h / 2.0, h / 2.0);

    if (idx.column() == ColConf) {
        const int d = 5;
        QColor dot = c; dot.setAlpha(235);
        p->setBrush(dot);
        p->drawEllipse(QPoint(chip.left() + 9, chip.center().y()), d / 2, d / 2);
    }

    p->setPen(c);
    p->setFont(f);
    p->drawText(chip, Qt::AlignCenter, text);
    p->restore();
}

QPixmap makeContactThumb(const QString& sensorTag, const QString& confLabel, bool fav, int px)
{
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pm(QSize(px, px) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    const QColor c = sensorColor(sensorTag);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF card(2.5, 2.5, px - 5.0, px - 5.0);
    QLinearGradient g(card.topLeft(), card.bottomRight());
    QColor top = c.lighter(120); top.setAlpha(70);
    QColor bot = c;              bot.setAlpha(26);
    g.setColorAt(0.0, top);
    g.setColorAt(1.0, bot);
    p.setPen(QPen(QColor(c.red(), c.green(), c.blue(), 140), 1.2));
    p.setBrush(g);
    p.drawRoundedRect(card, px * 0.16, px * 0.16);

    const QPointF ctr(card.center().x(), card.center().y() - px * 0.07);
    const qreal   r = px * 0.17;
    QPainterPath pin;
    pin.addEllipse(ctr, r, r);
    pin.moveTo(ctr.x() - r * 0.72, ctr.y() + r * 0.62);
    pin.lineTo(ctr.x(), ctr.y() + r * 1.95);
    pin.lineTo(ctr.x() + r * 0.72, ctr.y() + r * 0.62);
    p.setPen(Qt::NoPen);
    QColor pinc = c.lighter(125); pinc.setAlpha(240);
    p.setBrush(pinc);
    p.drawPath(pin);
    p.setBrush(QColor(0, 0, 0, 90));
    p.drawEllipse(ctr, r * 0.42, r * 0.42);

    QFont tf("Segoe UI"); tf.setBold(true); tf.setPixelSize(std::max(7, int(px * 0.14)));
    p.setFont(tf);
    p.setPen(QColor(c.lighter(150)));
    p.drawText(QRectF(0, card.bottom() - px * 0.30, px, px * 0.24),
               Qt::AlignHCenter | Qt::AlignVCenter, sensorTag);

    if (!confLabel.isEmpty()) {
        p.setPen(Qt::NoPen);
        p.setBrush(confidenceColor(confLabel));
        p.drawEllipse(QPointF(card.right() - px * 0.13, card.top() + px * 0.13), px * 0.055, px * 0.055);
    }
    if (fav) {
        QFont sf; sf.setPixelSize(std::max(8, int(px * 0.18)));
        p.setFont(sf);
        p.setPen(QColor(Theme::kWarning));
        p.drawText(QRectF(card.left() + 3, card.top() + 1, px * 0.34, px * 0.34),
                   Qt::AlignLeft | Qt::AlignTop, QStringLiteral("★"));
    }
    p.end();
    return pm;
}

QString contactSnapshotPath(app::Project* proj, uint64_t contact_id)
{
    if (!proj) return {};
    const std::string data = proj->dataPath();
    if (data.empty()) return {};
    return QString::fromStdString(data) + QStringLiteral("/contacts/")
         + QString::number(contact_id) + QStringLiteral(".png");
}

QPixmap contactThumbnail(app::Project* proj, const core::Contact& c, int px)
{
    // Persisted snapshot (the square grab taken when the contact was picked).
    const QString path = contactSnapshotPath(proj, c.id);
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        QPixmap pm(path);
        if (!pm.isNull()) {
            const int side = std::min(pm.width(), pm.height());
            const QPixmap sq = pm.copy((pm.width() - side) / 2,
                                       (pm.height() - side) / 2, side, side);
            const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
            QPixmap out = sq.scaled(QSize(px, px) * dpr,
                                    Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation);
            out.setDevicePixelRatio(dpr);
            return out;
        }
    }

    // Fallback: synthetic sensor tile (sensor tag from the contact's source layer).
    QString tag = QObject::tr("Map");
    if (proj && !c.line_id.empty())
        if (auto* layer = proj->findLayer(c.line_id))
            tag = modalityTag(layer->modality);
    return makeContactThumb(tag, confidenceLabel(c.confidence), isFavourite(c), px);
}

} // namespace dolphin::ui::cmvis
