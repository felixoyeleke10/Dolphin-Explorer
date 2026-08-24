#include "ui/features/contacts/ContactVisuals.h"
#include "ui/shared/contacts/ContactSymbols.h"
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

#include <algorithm>

namespace dolphin::ui::cmvis {

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
    QPainterPath pin = contactSymbolPath(QStringLiteral("pin"), r);
    const QRectF pin_bounds = pin.boundingRect();
    pin.translate(ctr - pin_bounds.center());
    p.setPen(Qt::NoPen);
    QColor pinc = c.lighter(125); pinc.setAlpha(240);
    p.setBrush(pinc);
    p.drawPath(pin);
    p.setBrush(QColor(0, 0, 0, 90));
    const QPointF head_center(
        ctr.x(), ctr.y() - r * 2.15 - pin_bounds.center().y());
    p.drawEllipse(head_center, r * 0.36, r * 0.36);

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
