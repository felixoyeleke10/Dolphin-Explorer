#pragma once
// Layout constants, colors, item roles, and PaletteDelegate
// shared by CommandPaletteDelegate.cpp and CommandPaletteDialog.cpp.
#include "ui/shared/dialogs/CommandPaletteDialog.h"

#include <QColor>
#include <QStyledItemDelegate>

namespace dolphin::ui {
namespace detail {

// -- Layout --------------------------------------------------------------------
constexpr int kCardW    = 560;
constexpr int kShadowX  = 8;
constexpr int kShadowB  = 10;
constexpr int kPalW     = kCardW + kShadowX * 2;
constexpr int kInputH   = 34;
constexpr int kRowH     = 32;
constexpr int kHdrH     = 18;
constexpr int kMaxItems = 8;
constexpr int kRadius   = 6;

// -- Item roles ----------------------------------------------------------------
enum {
    RoleItemIdx  = Qt::UserRole,
    RoleIsHeader = Qt::UserRole + 1,
    RoleCategory = Qt::UserRole + 2,
};

// -- Colors --------------------------------------------------------------------
// Command palette uses its own dark palette — intentionally distinct from the
// shell theme so the overlay reads as a separate floating surface.
inline const QColor kBg          { 0x1e, 0x1e, 0x1e };
inline const QColor kBorderOuter { 0x45, 0x45, 0x45 };
inline const QColor kAccent      { 0x00, 0x7a, 0xcc };   // focus indicator
inline const QColor kSelBg       { 0x09, 0x47, 0x71 };   // active selection
inline const QColor kTextPrimary { 0xcc, 0xcc, 0xcc };
inline const QColor kTextSubtle  { 0x85, 0x85, 0x85 };
inline const QColor kTextMuted   { 0x55, 0x55, 0x55 };
inline const QColor kDisabledFg  { 0x44, 0x44, 0x44 };
inline const QColor kHdrBg       { 0x25, 0x25, 0x25 };   // category header bg
inline const QColor kHdrSep      {255, 255, 255,  8};    // subtle top separator for category headers
inline const QColor kRowSelBg    {255, 255, 255, 10};    // selected row highlight tint
inline const QColor kCardShadow  {  0,   0,   0, 38};    // floating card drop-shadow

QColor categoryDot(const QString& cat);

// -- Delegate ------------------------------------------------------------------
class PaletteDelegate : public QStyledItemDelegate {
    const QList<CommandPaletteItem>* m_items;
public:
    explicit PaletteDelegate(const QList<CommandPaletteItem>* items, QObject* p);

    void  paint   (QPainter* p, const QStyleOptionViewItem& opt,
                   const QModelIndex& idx) const override;
    QSize sizeHint(const QStyleOptionViewItem& opt,
                   const QModelIndex& idx) const override;

private:
    void paintHeader(QPainter* p, const QStyleOptionViewItem& opt,
                     const QModelIndex& idx) const;
    void paintItem  (QPainter* p, const QStyleOptionViewItem& opt,
                     const QModelIndex& idx) const;
};

} // namespace detail
} // namespace dolphin::ui
