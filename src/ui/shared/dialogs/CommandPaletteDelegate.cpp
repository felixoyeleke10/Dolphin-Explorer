// CommandPaletteDelegate.cpp — PaletteDelegate paint implementation.
#include "ui/shared/dialogs/CommandPaletteDelegate.h"

#include <QFontMetrics>
#include <QPainter>

namespace dolphin::ui {
namespace detail {

// ── Delegate ──────────────────────────────────────────────────────────────────

PaletteDelegate::PaletteDelegate(const QList<CommandPaletteItem>* items, QObject* p)
    : QStyledItemDelegate(p), m_items(items) {}

void PaletteDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt,
                             const QModelIndex& idx) const
{
    p->save();

    if (idx.data(RoleIsHeader).toBool())
        paintHeader(p, opt, idx);
    else
        paintItem(p, opt, idx);

    p->restore();
}

QSize PaletteDelegate::sizeHint(const QStyleOptionViewItem&,
                                 const QModelIndex& idx) const
{
    return { kCardW, idx.data(RoleIsHeader).toBool() ? kHdrH : kRowH };
}

void PaletteDelegate::paintHeader(QPainter* p, const QStyleOptionViewItem& opt,
                                   const QModelIndex& idx) const
{
    if (opt.rect.top() > 0)
        p->fillRect(QRect(0, opt.rect.top(), opt.rect.width(), 1), kHdrSep);

    QFont f = opt.font;
    f.setPointSizeF(opt.font.pointSizeF() * 0.65);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0.7);
    p->setFont(f);
    p->setPen(kTextMuted);
    p->drawText(opt.rect.adjusted(14, 0, -12, 0),
                Qt::AlignVCenter | Qt::AlignLeft,
                idx.data(RoleCategory).toString().toUpper());
}

void PaletteDelegate::paintItem(QPainter* p, const QStyleOptionViewItem& opt,
                                 const QModelIndex& idx) const
{
    const int i = idx.data(RoleItemIdx).toInt();
    if (i < 0 || i >= m_items->size()) return;
    const CommandPaletteItem& item = (*m_items)[i];

    const bool sel     = (opt.state & QStyle::State_Selected) && item.enabled;
    const bool enabled = item.enabled;

    if (sel)
        p->fillRect(opt.rect, kRowSelBg);

    // Shortcut — plain text on the right, no pill badges
    int rightEdge = opt.rect.right() - 14;
    if (!item.shortcut.isEmpty() && enabled) {
        QFont sf = opt.font;
        sf.setPointSizeF(opt.font.pointSizeF() * 0.80);
        const QFontMetrics sfm(sf);
        const int scW = sfm.horizontalAdvance(item.shortcut);
        p->setFont(sf);
        p->setPen(kTextMuted);
        p->drawText(QRect(rightEdge - scW, opt.rect.top(), scW, opt.rect.height()),
                    Qt::AlignVCenter | Qt::AlignLeft, item.shortcut);
        rightEdge -= scW + 12;
    }

    // Label
    QFont lf = opt.font;
    lf.setPointSizeF(opt.font.pointSizeF() * 0.93);
    p->setFont(lf);
    p->setPen(enabled ? (sel ? Qt::white : kTextPrimary) : kDisabledFg);
    const QRect lr(14, opt.rect.top(), rightEdge - 14 - 4, opt.rect.height());
    p->drawText(lr, Qt::AlignVCenter | Qt::AlignLeft,
                QFontMetrics(lf).elidedText(item.label, Qt::ElideRight, lr.width()));
}

} // namespace detail
} // namespace dolphin::ui
