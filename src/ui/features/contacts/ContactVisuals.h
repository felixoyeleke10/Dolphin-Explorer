#pragma once
// ContactVisuals — model constants (columns, nav-node kinds, item roles), small
// colour/label helpers, the sensor-chip delegate, and the thumbnail painter shared
// by the Contact Manager's aspect files (.Layout / .View / .Commands). Kept in the
// dolphin::ui::cmvis namespace; each aspect file pulls it in with `using namespace`.
#include <QColor>
#include <QIcon>
#include <QPainterPath>
#include <QPixmap>
#include <QString>
#include <QStyledItemDelegate>
#include "app/layers/LayerUtils.h"   // app::Modality
#include "core/Contact.h"

class QPainter;

namespace dolphin::app { class Project; }

namespace dolphin::ui::cmvis {

// Details-table columns.
enum Col { ColLabel, ColSensor, ColSource, ColClass, ColConf, ColLat, ColLon, ColDepth, ColRange, ColCount };
// Nav-tree node kinds.
enum NavKind { NavAll, NavFav, NavModality, NavMap, NavLine, NavBucket, NavFacetHeader, NavGroup, NavRecycle };
// "Group by" facets.
enum GroupFacet { GroupSensor, GroupClass, GroupConfidence, GroupGroup, GroupDate, GroupLabel };

inline constexpr int RoleKind      = Qt::UserRole;
inline constexpr int RoleModality  = Qt::UserRole + 1;
inline constexpr int RoleLineId    = Qt::UserRole + 2;
inline constexpr int RoleBucketKey = Qt::UserRole + 3;
inline constexpr int RoleFacet     = Qt::UserRole + 4;
inline constexpr int RoleGroupId   = Qt::UserRole + 5;

inline constexpr char kFavTag[] = "favourite";
inline constexpr app::Modality kSensorOrder[] = {
    app::Modality::Sidescan, app::Modality::SubBottom,
    app::Modality::Magnetometer, app::Modality::Multibeam, app::Modality::Raster,
};

bool    isFavourite(const core::Contact& c);
QString modalityTag(app::Modality m);
QString confidenceLabel(core::Confidence c);
QString sensorFolderLabel(app::Modality m);
QColor  sensorColor(const QString& tag);
QColor  confidenceColor(const QString& label);
// Bucket a contact falls into for a non-sensor facet (display name == match key).
QString bucketKey(int facet, const core::Contact& c, app::Project* proj);
int     confidenceRank(const QString& label);   // ordering for the Confidence facet
// Explorer-style thumbnail tile: rounded sensor card + map-pin + confidence dot + star.
QPixmap makeContactThumb(const QString& sensorTag, const QString& confLabel, bool fav, int px);

// Filesystem path of a contact's persisted snapshot PNG (<project data>/contacts/<id>.png),
// or empty when the project has no data dir. The snapshot is a derived artifact keyed on
// the stable contact id — no entry in the .dlp is needed.
QString contactSnapshotPath(app::Project* proj, uint64_t contact_id);

// The contact's square thumbnail at `px`: the persisted snapshot (centre-cropped to a
// square, scaled) when one exists, otherwise the synthetic makeContactThumb tile.
QPixmap contactThumbnail(app::Project* proj, const core::Contact& c, int px);

// -- Map marker symbol library --------------------------------------------------
// Shared by the symbol picker (ContactEditorDialog), the right-click quick-set
// menu (LineListPanel), and the map painter, so all three read one definition
// and can never silently drift apart. id "" (Auto) means "let the system decide"
// — currently the historical diamond, same as an unrecognized/legacy id.
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

// Paints the Sensor / Confidence table columns as rounded colour chips.
class ChipDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
};

} // namespace dolphin::ui::cmvis
