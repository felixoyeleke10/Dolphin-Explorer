#pragma once
// ContactSymbols — the map-marker symbol library shared by the symbol picker
// (ContactEditorDialog, dolphin-ui-contacts), the right-click quick-set menu
// (LineListPanel, dolphin-ui-shared), and the map painter (MapView,
// dolphin-ui-map), so all three read one definition and can never silently
// drift apart. Lives in dolphin-ui-shared — the lowest of those three targets
// in the link graph — so none of them need a new cross-target dependency.
#include <QColor>
#include <QIcon>
#include <QPainterPath>
#include <QString>

namespace dolphin::ui {

// id "" (Auto) means "let the system decide" — currently the historical
// diamond, same as an unrecognized/legacy id.
struct ContactSymbolOpt { const char* label; const char* id; };
extern const ContactSymbolOpt kContactSymbols[];
extern const int kContactSymbolCount;

// Marker outline for `symbol_id` (one of kContactSymbols[i].id), centred at the
// origin with circumradius `radius`, in local unrotated/unscaled coordinates —
// translate the painter to the marker's pixel position before filling/stroking.
// Not all shapes are centred on their own bounding box (the pin's tip, not its
// centroid, sits at the origin — see contactSymbolPath's definition).
QPainterPath contactSymbolPath(const QString& symbol_id, qreal radius);

// A square `px`x`px` QIcon preview of `symbol_id`, fitted and centred within the
// icon regardless of the shape's own bounding-box asymmetry (e.g. the pin).
// Used by the symbol picker combo and the right-click quick-set menu.
QIcon contactSymbolIcon(const QString& symbol_id, int px,
                        const QColor& fill, const QColor& outline);

} // namespace dolphin::ui
