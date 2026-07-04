#pragma once
// Layout constants, colors, item roles, and PaletteDelegate
// shared by CommandPaletteDelegate.cpp and CommandPaletteDialog.cpp.
#include "ui/shared/dialogs/CommandPaletteDialog.h"
#include "ui/shell/Theme.h"

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
// Command palette uses its own compact palette — intentionally distinct from
// the shell theme so the overlay reads as a separate floating surface. Every
// colour resolves through Theme::mode() so the palette follows Light/Dark.
inline bool paletteLight() { return Theme::mode() == Theme::Mode::Light; }
inline QColor pc(QColor dark, QColor light) { return paletteLight() ? light : dark; }

inline QColor kBg()          { return pc({0x1e,0x1e,0x1e}, {0xff,0xff,0xff}); }
inline QColor kBorderOuter() { return pc({0x45,0x45,0x45}, {0xc4,0xc4,0xc9}); }
inline QColor kAccent()      { return pc({0x00,0x7a,0xcc}, {0x00,0x7a,0xcc}); }  // focus indicator
inline QColor kSelBg()       { return pc({0x09,0x47,0x71}, {0xcc,0xe0,0xf5}); }  // active selection
inline QColor kTextPrimary() { return pc({0xcc,0xcc,0xcc}, {0x1c,0x1c,0x1e}); }
inline QColor kTextSubtle()  { return pc({0x85,0x85,0x85}, {0x54,0x54,0x5a}); }
inline QColor kTextMuted()   { return pc({0x55,0x55,0x55}, {0x7a,0x7a,0x80}); }
inline QColor kDisabledFg()  { return pc({0x44,0x44,0x44}, {0xb8,0xb8,0xbd}); }
inline QColor kHdrBg()       { return pc({0x25,0x25,0x25}, {0xf2,0xf2,0xf4}); }  // category header bg
inline QColor kHdrSep()      { return pc({255,255,255, 8}, {0,0,0, 12}); }       // header top separator
inline QColor kRowSelBg()    { return pc({255,255,255,10}, {0,0,0, 14}); }       // hover/selected row tint
inline QColor kCardShadow()  { return QColor(0, 0, 0, 38); }                     // drop-shadow (both modes)

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
