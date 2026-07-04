// CommandPaletteDialog.cpp — dialog shell, painting, filter logic, event handling.
//   Delegate + constants → CommandPaletteDelegate.h / CommandPaletteDelegate.cpp
#include "ui/shared/dialogs/CommandPaletteDialog.h"
#include "ui/shared/UiUtils.h"
#include "ui/shared/dialogs/CommandPaletteDelegate.h"
#include "ui/shell/Theme.h"

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace dolphin::ui {

using namespace detail;

// -- Construction --------------------------------------------------------------

CommandPaletteDialog::CommandPaletteDialog(QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
{
    setObjectName("cpRoot");
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(kPalW);
    setModal(false);

    // Shadow margins: kShadowX on sides, kShadowB at bottom, 4px top
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(kShadowX, 4, kShadowX, kShadowB);
    root->setSpacing(0);

    // -- Card container --------------------------------------------------------
    auto* card = new QWidget(this);
    card->setObjectName("cpCard");
    auto* cl = makeCompactLayout<QVBoxLayout>(card);
    root->addWidget(card);

    // -- Search row ------------------------------------------------------------
    auto* input_row = new QWidget(card);
    input_row->setObjectName("cpInputRow");
    auto* irl = new QHBoxLayout(input_row);
    irl->setContentsMargins(Theme::kSpacing4, 0, Theme::kSpacing4, 0);
    irl->setSpacing(0);

    m_input = new QLineEdit(input_row);
    m_input->setObjectName("cpInput");
    m_input->setPlaceholderText(tr("Search commands…"));
    m_input->installEventFilter(this);
    irl->addWidget(m_input, 1);

    input_row->setFixedHeight(kInputH);
    cl->addWidget(input_row);

    // Separator
    auto* sep = new QFrame(card);
    sep->setObjectName("cpSep");
    sep->setFrameShape(QFrame::NoFrame);
    sep->setFixedHeight(Theme::kSepSz);
    cl->addWidget(sep);

    // -- Results list ----------------------------------------------------------
    m_list = new QListWidget(card);
    m_list->setObjectName("cpList");
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setItemDelegate(new PaletteDelegate(&m_all, this));
    m_list->setFocusPolicy(Qt::NoFocus);
    cl->addWidget(m_list);

    connect(m_input, &QLineEdit::textChanged, this, &CommandPaletteDialog::applyFilter);
    connect(m_list, &QListWidget::itemActivated,
            this, [this](QListWidgetItem*) { executeSelected(); });

    qApp->installEventFilter(this);
}

// -- Painting ------------------------------------------------------------------

void CommandPaletteDialog::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipRect(rect());

    // Card rect sits inside the shadow margins; width adapts to dialog size.
    const int cardW = width() - kShadowX * 2;
    const QRectF card(kShadowX, 4, cardW, height() - 4 - kShadowB);

    // Single subtle shadow
    p.setPen(Qt::NoPen);
    p.setBrush(kCardShadow());
    p.drawRoundedRect(card.adjusted(-1, 3, 1, 6), kRadius + 1, kRadius + 1);

    // Card background
    p.setBrush(kBg());
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(card, kRadius, kRadius);

    // Border — 1px, slightly lighter than bg
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(kBorderOuter(), 1.0));
    p.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
}

void CommandPaletteDialog::resizeEvent(QResizeEvent* ev) { QDialog::resizeEvent(ev); }

// -- Public API ----------------------------------------------------------------

void CommandPaletteDialog::setItems(QList<CommandPaletteItem> items)
{
    m_all = std::move(items);
}

void CommandPaletteDialog::popup(QWidget* anchor)
{
    if (anchor && !anchor->isWindow()) {
        // Anchor is the unified bar — drop the palette directly below it,
        // sized to match its width so it looks like an attached dropdown.
        setFixedWidth(anchor->width() + kShadowX * 2);
        const QPoint bl = anchor->mapToGlobal(QPoint(0, anchor->height() + 2));
        move(bl.x() - kShadowX, bl.y());
    } else {
        // Fallback: anchor is the window — centre near the top.
        QWidget* win = anchor;
        if (win) {
            const QRect wg = win->frameGeometry();
            setFixedWidth(kPalW);
            move(wg.x() + (wg.width() - kPalW) / 2,
                 wg.y() + qRound(wg.height() * 0.13));
        }
    }

    { QSignalBlocker sb(m_input); m_input->clear(); }
    applyFilter(QString{});

    show();
    raise();
    activateWindow();
    m_input->setFocus();
}

// -- Filter & rendering --------------------------------------------------------

void CommandPaletteDialog::applyFilter(const QString& text)
{
    m_list->clear();
    const QString lo = text.trimmed().toLower().mid(
        text.trimmed().startsWith('/') ? 1 : 0);

    // Bucket matching items by category, preserving first-seen order
    QList<QString>        catOrder;
    QHash<QString, QList<int>> groups;

    for (int i = 0; i < m_all.size(); ++i) {
        const auto& item = m_all[i];
        if (lo.isEmpty()) {
            if (item.searchOnly) continue;
        } else {
            const bool hit = item.label.toLower().contains(lo)
                          || item.category.toLower().contains(lo)
                          || item.aliases.toLower().contains(lo);
            if (!hit) continue;
        }
        if (!groups.contains(item.category)) {
            catOrder.append(item.category);
            groups[item.category] = {};
        }
        groups[item.category].append(i);
    }

    // Populate list: section header + items per category
    bool firstEnabled = false;
    int  itemsShown   = 0;

    for (const QString& cat : catOrder) {
        if (itemsShown >= kMaxItems) break;

        // Section header
        auto* hdr = new QListWidgetItem;
        hdr->setData(RoleIsHeader, true);
        hdr->setData(RoleCategory, cat);
        hdr->setData(RoleItemIdx,  -1);
        hdr->setSizeHint({ kCardW, kHdrH });
        hdr->setFlags(Qt::NoItemFlags);
        m_list->addItem(hdr);

        for (int idx : groups[cat]) {
            if (itemsShown >= kMaxItems) break;
            const auto& item = m_all[idx];

            auto* li = new QListWidgetItem;
            li->setData(RoleIsHeader, false);
            li->setData(RoleItemIdx,  idx);
            li->setSizeHint({ kCardW, kRowH });
            if (!item.enabled)
                li->setFlags(li->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
            m_list->addItem(li);

            if (!firstEnabled && item.enabled) {
                m_list->setCurrentItem(li);
                firstEnabled = true;
            }
            ++itemsShown;
        }
    }

    updateHeight();
}

void CommandPaletteDialog::executeSelected()
{
    auto* li = m_list->currentItem();
    if (!li || !(li->flags() & Qt::ItemIsEnabled)) return;
    if (li->data(RoleIsHeader).toBool()) return;
    const int i = li->data(RoleItemIdx).toInt();
    if (i < 0 || i >= m_all.size()) return;
    const auto& item = m_all[i];
    if (!item.enabled || !item.action) return;
    hide();
    item.action();
}

void CommandPaletteDialog::moveHighlight(int delta)
{
    const int n = m_list->count();
    if (n == 0) return;
    int row = m_list->currentRow();
    for (int step = 0; step < n; ++step) {
        row = std::clamp(row + delta, 0, n - 1);
        const auto* it = m_list->item(row);
        if (!it->data(RoleIsHeader).toBool() && (it->flags() & Qt::ItemIsEnabled))
            break;
    }
    m_list->setCurrentRow(row);
    m_list->scrollToItem(m_list->item(row));
}

void CommandPaletteDialog::updateHeight()
{
    // Sum up actual item heights (headers + rows) up to kMaxItems regular rows
    int totalH = 0;
    int items  = 0;
    for (int r = 0; r < m_list->count(); ++r) {
        const auto* it     = m_list->item(r);
        const bool  isHdr  = it->data(RoleIsHeader).toBool();
        totalH += isHdr ? kHdrH : kRowH;
        if (!isHdr) ++items;
        if (items >= kMaxItems) break;
    }

    m_list->setFixedHeight(totalH);
    // Full dialog height = top margin(4) + card contents + bottom shadow(kShadowB)
    setFixedHeight(4 + kInputH + 1 + totalH + kShadowB);
}

// -- Event filter --------------------------------------------------------------

bool CommandPaletteDialog::eventFilter(QObject* watched, QEvent* ev)
{
    if (!isVisible()) return QDialog::eventFilter(watched, ev);

    switch (ev->type()) {
    case QEvent::KeyPress: {
        auto* ke = static_cast<QKeyEvent*>(ev);
        switch (ke->key()) {
        case Qt::Key_Escape:  hide();            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:   executeSelected(); return true;
        case Qt::Key_Up:      moveHighlight(-1); return true;
        case Qt::Key_Down:    moveHighlight(+1); return true;
        default: break;
        }
        break;
    }
    case QEvent::MouseButtonPress: {
        const QPoint gp = static_cast<QMouseEvent*>(ev)->globalPosition().toPoint();
        if (!geometry().contains(gp))
            QTimer::singleShot(0, this, &QWidget::hide);
        break;
    }
    case QEvent::ApplicationDeactivate:
        hide();
        break;
    default:
        break;
    }

    return QDialog::eventFilter(watched, ev);
}

} // namespace dolphin::ui
