// LineListPanel.Tree.cpp — tree construction: buildTree and all section builders.
#include "ui/shared/panels/LineListPanel.h"
#include "ui/shared/panels/LineListPanel_p.h"
#include "app/layers/DataLayer.h"
#include "app/layers/LayerUtils.h"
#include "app/project/Project.h"
#include "core/Contact.h"
#include "ui/shell/Theme.h"
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QIcon>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>
#include <QStyle>
#include <QTreeWidget>
#include <functional>
#include <map>

using namespace dolphin::ui::detail;

namespace dolphin::ui {

namespace {

// State colors matching DataLibraryWindow's colour scheme.
constexpr QRgb kClrReady   = 0xff28a745u;
constexpr QRgb kClrFailed  = 0xffdc3545u;
constexpr QRgb kClrPending = 0xfffd7e14u;

QColor mutedText() { return QColor(Theme::kTextMuted); }
QColor softText()  { return QColor(Theme::kTextSoft); }

// Resolve a tag id to its palette color. Falls back to hash-derived hue for
// any id not in the palette (e.g. old text tags loaded from disk).
static QColor tagColorById(const std::string& id)
{
    for (const auto& e : kTagPalette)
        if (e.id == id) return QColor(e.r, e.g, e.b);
    return QColor::fromHsl(static_cast<int>(qHash(QString::fromStdString(id)) % 360), 170, 115);
}

// Composite icon: up to 4 colored circles in a fixed 16×16 square, placed
// before the item text via Qt's decoration role. The painter is scoped so it
// ends before QIcon copies the pixmap data.
static QIcon makeTagIcon(const std::vector<std::string>& tags)
{
    if (tags.empty()) return {};
    constexpr int kSize = 16;
    const int n = std::min(static_cast<int>(tags.size()), 4);
    const int d   = (n == 1) ? 10 : (n == 2) ? 6 : (n <= 3) ? 4 : 3;
    const int gap = (n == 1) ?  0 : (n == 4) ? 1 : 2;
    const int total = n * d + (n - 1) * gap;
    const int x0  = (kSize - total) / 2;
    const int y0  = (kSize - d) / 2;

    QPixmap pm(kSize, kSize);
    pm.fill(Qt::transparent);
    {
        QPainter pa(&pm);
        pa.setRenderHint(QPainter::Antialiasing);
        pa.setPen(Qt::NoPen);
        for (int i = 0; i < n; ++i) {
            pa.setBrush(tagColorById(tags[static_cast<size_t>(i)]));
            pa.drawEllipse(x0 + i * (d + gap), y0, d, d);
        }
    }  // painter ends — pixmap is fully committed before QIcon copies it
    return QIcon(pm);
}

QString modalityDisplay(dolphin::app::Modality modality)
{
    using M = dolphin::app::Modality;
    switch (modality) {
    case M::Sidescan:     return QObject::tr("Sidescan Sonar");
    case M::SubBottom:    return QObject::tr("Sub-bottom Profiler");
    case M::Multibeam:    return QObject::tr("Multibeam");
    case M::Magnetometer: return QObject::tr("Magnetometer");
    case M::Raster:       return QObject::tr("Raster");
    default:              return QObject::tr("Other");
    }
}

// Display text for a layer — label only; tags are shown as colored dots via the item icon.
QString layerDisplayText(const dolphin::app::DataLayer* layer)
{
    if (!layer) return {};
    return QString::fromStdString(layer->label);
}

// Display text for a contact — label + classification + range + layer + tags.
QString contactDisplayText(const dolphin::core::Contact& contact,
                           dolphin::app::Project* project,
                           bool include_layer_label)
{
    QStringList parts;
    parts << QString::fromStdString(contact.label);
    if (!contact.classification.empty())
        parts << QString::fromStdString(contact.classification);
    if (contact.range_m > 0.f)
        parts << QString("%1 m").arg(contact.range_m, 0, 'f', 1);
    if (include_layer_label && project && !contact.line_id.empty()) {
        if (const auto* layer = project->findLayer(contact.line_id))
            parts << QString::fromStdString(layer->label);
    }
    return parts.join("  –  ");  // Tags shown as colored dots via item icon.
}

// Display text for a feature — label + type + classification + vertex count.
static QString featureDisplayText(const dolphin::core::Feature& f)
{
    QStringList parts;
    parts << QString::fromStdString(f.label);
    parts << (f.type == dolphin::core::FeatureType::Polygon
                  ? LineListPanel::tr("Polygon")
                  : LineListPanel::tr("Line"));
    if (!f.classification.empty())
        parts << QString::fromStdString(f.classification);
    parts << LineListPanel::tr("%n pt(s)", "", static_cast<int>(f.vertices.size()));
    return parts.join("  –  ");
}

static void applyContactTagDecoration(QTreeWidgetItem* item, const dolphin::core::Contact& c)
{
    if (c.tags.empty()) return;
    item->setIcon(0, makeTagIcon(c.tags));
    QStringList tl;
    for (const auto& t : c.tags) tl << ("#" + QString::fromStdString(t));
    item->setToolTip(0, tl.join("  "));
}

// Creates a collapsible section header item. If parent is non-null the item
// is a child; otherwise it is a tree root.
QTreeWidgetItem* makeSectionItem(QTreeWidget* tree, QTreeWidgetItem* parent,
                                  const QString& title)
{
    auto* item = parent ? new QTreeWidgetItem(parent, QStringList{title})
                        : new QTreeWidgetItem(tree,   QStringList{title});
    item->setFlags(Qt::ItemIsEnabled);
    return item;
}

void applyLayerStateDecoration(QTreeWidgetItem* item, app::LayerState state)
{
    switch (state) {
        case app::LayerState::Ready:
            item->setForeground(0, QBrush(QColor(kClrReady)));
            item->setToolTip(0, {});
            break;
        case app::LayerState::Failed:
            item->setForeground(0, QBrush(QColor(kClrFailed)));
            item->setToolTip(0, QObject::tr("Import failed"));
            break;
        case app::LayerState::Indexing:
        case app::LayerState::Placeholder:
            item->setForeground(0, QBrush(QColor(kClrPending)));
            item->setToolTip(0, QObject::tr("Indexing") + QString(QChar(0x2026)));
            break;
    }
}

} // namespace

// -- Public helpers ------------------------------------------------------------

QTreeWidgetItem* LineListPanel::makeSectionHeader(const QString& title)
{
    return makeSectionItem(m_tree, nullptr, title);
}

QTreeWidgetItem* LineListPanel::makeEmptyPlaceholder(QTreeWidgetItem* parent,
                                                      const QString& text)
{
    auto* item = new QTreeWidgetItem(parent, QStringList{text});
    item->setFlags(Qt::ItemIsEnabled);
    item->setForeground(0, mutedText());
    return item;
}

// -- Tree construction ---------------------------------------------------------

void LineListPanel::buildTree()
{
    if (!m_project) {
        auto* msg = new QTreeWidgetItem(m_tree, QStringList{tr("No project open")});
        msg->setFlags(Qt::ItemIsEnabled);
        msg->setForeground(0, mutedText());
        QFont f = msg->font(0); f.setItalic(true); f.setPixelSize(11);
        msg->setFont(0, f);
        return;
    }

    if (m_mode == ContentMode::LayersOnly) {
        buildLayersSection(nullptr);
        buildContactsSection(nullptr);
        return;
    }

    buildProjectSection();
}

void LineListPanel::buildProjectSection()
{
    const QString name = QString::fromStdString(m_project->name());
    auto* proj = makeSectionItem(m_tree, nullptr,
                                 name.isEmpty() ? tr("Project") : name);
    proj->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    setItemType(proj, ItemType::Project);

    buildLayersSection(proj);
    buildContactsSection(proj);
    buildFeaturesSection(proj);
}

void LineListPanel::buildContactsSection(QTreeWidgetItem* parent)
{
    auto* sec = makeSectionItem(m_tree, parent, tr("Contacts"));
    setItemType(sec, ItemType::ContactsSection);

    const auto& contacts  = m_project->contacts();
    const auto& cgrps     = m_project->contactGroups();

    if (contacts.empty() && cgrps.empty()) {
        makeEmptyPlaceholder(sec, tr("No contacts"));
        return;
    }

    // User-defined contact groups first
    for (const auto& grp : cgrps) {
        auto* gi = new QTreeWidgetItem(sec, QStringList{QString::fromStdString(grp.name)});
        gi->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        setItemType(gi, ItemType::ContactGroup);
        gi->setData(0, kRoleGroupId, QString::fromStdString(grp.id));
        gi->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_DirIcon));
        QFont f = gi->font(0); f.setBold(true); gi->setFont(0, f);

        bool any = false;
        for (const auto& c : contacts) {
            if (c.group_id != grp.id) continue;
            auto* item = new QTreeWidgetItem(
                gi, QStringList{contactDisplayText(c, m_project, true)});
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            setItemType(item, ItemType::Contact);
            item->setData(0, kRoleId, static_cast<qulonglong>(c.id));
            item->setForeground(0, softText());
            applyContactTagDecoration(item, c);
            any = true;
        }
        if (!any) makeEmptyPlaceholder(gi, tr("Empty group"));
    }

    // Ungrouped contacts
    bool any_ungrouped = false;
    for (const auto& c : contacts) {
        if (!c.group_id.empty()) continue;
        auto* item = new QTreeWidgetItem(
            sec, QStringList{contactDisplayText(c, m_project, true)});
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        setItemType(item, ItemType::Contact);
        item->setData(0, kRoleId, static_cast<qulonglong>(c.id));
        item->setForeground(0, softText());
        any_ungrouped = true;
    }
    if (!any_ungrouped && cgrps.empty())
        makeEmptyPlaceholder(sec, tr("No contacts"));
}

void LineListPanel::buildFeaturesSection(QTreeWidgetItem* parent)
{
    auto* sec = makeSectionItem(m_tree, parent, tr("Features"));
    setItemType(sec, ItemType::FeaturesSection);

    const auto& features = m_project->features();
    if (features.empty()) {
        makeEmptyPlaceholder(sec, tr("No features yet"));
        return;
    }
    for (const auto& f : features) {
        auto* item = new QTreeWidgetItem(sec, QStringList{featureDisplayText(f)});
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        setItemType(item, ItemType::Feature);
        item->setData(0, kRoleId, static_cast<qulonglong>(f.id));
        item->setForeground(0, softText());
    }
}

// Returns the resolved modality for a layer (same logic used in the ungrouped bucket loop).
static app::Modality resolveModality(const app::DataLayer* layer)
{
    if (!layer) return app::Modality::Unknown;
    if (layer->modality != app::Modality::Unknown) return layer->modality;
    if (layer->index_built) return app::inferModality(layer->artifact_index);
    return app::Modality::Unknown;
}

// -- Layer item helper ---------------------------------------------------------

static void addLayerItem(QTreeWidgetItem* parent, const app::DataLayer* layer)
{
    const bool is_ready = (layer->state == app::LayerState::Ready);
    auto* item = new QTreeWidgetItem(parent, QStringList{layerDisplayText(layer)});
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
    if (is_ready)
        flags |= Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
    item->setFlags(flags);
    item->setData(0, kRoleId,       QString::fromStdString(layer->id));
    setItemType(item, ItemType::Layer);
    item->setData(0, kRoleModality, static_cast<int>(layer->modality));
    item->setCheckState(0, layer->visible ? Qt::Checked : Qt::Unchecked);
    applyLayerStateDecoration(item, layer->state);

    // Bottom-tracked lines (auto / manual / both picks present) are shown in bold so
    // the user can see at a glance which data has a seabed pick.
    const bool bottom_tracked =
        layer->bottom_track_kind == app::BottomTrackKind::Auto   ||
        layer->bottom_track_kind == app::BottomTrackKind::Manual ||
        layer->bottom_track_kind == app::BottomTrackKind::Mixed;
    if (bottom_tracked) {
        QFont f = item->font(0);
        f.setBold(true);
        item->setFont(0, f);
    }

    if (!layer->tags.empty()) {
        item->setIcon(0, makeTagIcon(layer->tags));
        if (layer->state == app::LayerState::Ready) {
            QStringList tl;
            for (const auto& t : layer->tags) tl << ("#" + QString::fromStdString(t));
            item->setToolTip(0, tl.join("  "));
        }
    }
}

void LineListPanel::buildLayersSection(QTreeWidgetItem* parent)
{
    const auto& lgrps  = m_project->layerGroups();
    const auto& layers = m_project->layers();

    // -- Helpers ---------------------------------------------------------------

    // Lazy-create the per-modality section bucket (e.g. "Sidescan Sonar").
    std::map<app::Modality, QTreeWidgetItem*> mod_buckets;
    auto getOrCreateBucket = [&](app::Modality mod) -> QTreeWidgetItem* {
        auto it = mod_buckets.find(mod);
        if (it != mod_buckets.end()) return it->second;
        auto* g = parent ? new QTreeWidgetItem(parent, QStringList{modalityDisplay(mod)})
                         : new QTreeWidgetItem(m_tree,  QStringList{modalityDisplay(mod)});
        g->setFlags(Qt::ItemIsEnabled | Qt::ItemIsDropEnabled);
        setItemType(g, ItemType::Modality);
        mod_buckets[mod] = g;
        return g;
    };

    // Determine the single modality shared by all layers in a group.
    // Returns Unknown if the group is empty OR contains mixed modalities.
    auto groupModality = [&](const app::ItemGroup& grp) -> app::Modality {
        app::Modality mod = app::Modality::Unknown;
        for (const auto& layer : layers) {
            if (!layer || layer->group_id != grp.id) continue;
            const app::Modality lm = resolveModality(layer.get());
            if (mod == app::Modality::Unknown) { mod = lm; }
            else if (mod != lm) { return app::Modality::Unknown; }  // mixed
        }
        return mod;
    };

    // Helper to build the group QTreeWidgetItem under a given container.
    auto makeGroupItem = [&](QTreeWidgetItem* container, const app::ItemGroup& grp) {
        auto* gi = new QTreeWidgetItem(container, QStringList{QString::fromStdString(grp.name)});
        gi->setFlags(Qt::ItemIsEnabled | Qt::ItemIsDropEnabled | Qt::ItemIsSelectable);
        setItemType(gi, ItemType::LayerGroup);
        gi->setData(0, kRoleGroupId, QString::fromStdString(grp.id));
        gi->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_DirIcon));
        QFont f = gi->font(0); f.setBold(true); gi->setFont(0, f);
        bool any = false;
        for (const auto& layer : layers) {
            if (!layer || layer->group_id != grp.id) continue;
            addLayerItem(gi, layer.get());
            any = true;
        }
        if (!any) makeEmptyPlaceholder(gi, tr("Empty group"));
    };

    // -- Layer groups ----------------------------------------------------------
    // Single-modality groups nest inside their modality bucket so the sensor
    // header ("Sidescan Sonar") remains visible as the parent container.
    // Mixed-modality or empty groups appear at top level.
    for (const auto& grp : lgrps) {
        const app::Modality gmod = groupModality(grp);
        if (gmod != app::Modality::Unknown) {
            makeGroupItem(getOrCreateBucket(gmod), grp);
        } else {
            // Empty / cross-modality group — top level
            auto* container = parent ? parent
                                     : static_cast<QTreeWidgetItem*>(m_tree->invisibleRootItem());
            makeGroupItem(container, grp);
        }
    }

    // -- Ungrouped layers bucketed by modality ---------------------------------
    for (const auto& layer : layers) {
        if (!layer || !layer->group_id.empty()) continue;
        addLayerItem(getOrCreateBucket(resolveModality(layer.get())), layer.get());
    }

    if (layers.empty() && lgrps.empty()) {
        auto* placeholder = parent ? new QTreeWidgetItem(parent, QStringList{tr("No layers yet")})
                                   : new QTreeWidgetItem(m_tree,  QStringList{tr("No layers yet")});
        placeholder->setFlags(Qt::ItemIsEnabled);
        placeholder->setForeground(0, mutedText());
    }
}

// -- Targeted updates ----------------------------------------------------------

void LineListPanel::refreshContacts()
{
    if (!m_project) return;

    QTreeWidgetItem* sec = nullptr;
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        if (itemTypeOf(*it) == ItemType::ContactsSection) {
            sec = *it;
            break;
        }
        ++it;
    }
    if (!sec) { refresh(); return; }

    m_rebuilding = true;
    {
        QSignalBlocker sb(m_tree);
        while (sec->childCount())
            delete sec->takeChild(0);

        const auto& contacts = m_project->contacts();
        const auto& cgrps    = m_project->contactGroups();

        if (contacts.empty() && cgrps.empty()) {
            makeEmptyPlaceholder(sec, tr("No contacts"));
        } else {
            for (const auto& grp : cgrps) {
                auto* gi = new QTreeWidgetItem(sec, QStringList{QString::fromStdString(grp.name)});
                gi->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                setItemType(gi, ItemType::ContactGroup);
                gi->setData(0, kRoleGroupId, QString::fromStdString(grp.id));
                gi->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_DirIcon));
                QFont f = gi->font(0); f.setBold(true); gi->setFont(0, f);
                bool any = false;
                for (const auto& c : contacts) {
                    if (c.group_id != grp.id) continue;
                    auto* item = new QTreeWidgetItem(
                        gi, QStringList{contactDisplayText(c, m_project, true)});
                    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                    setItemType(item, ItemType::Contact);
                    item->setData(0, kRoleId, static_cast<qulonglong>(c.id));
                    item->setForeground(0, softText());
            applyContactTagDecoration(item, c);
                    any = true;
                }
                if (!any) makeEmptyPlaceholder(gi, tr("Empty group"));
            }
            bool any_ungrouped = false;
            for (const auto& c : contacts) {
                if (!c.group_id.empty()) continue;
                auto* item = new QTreeWidgetItem(
                    sec, QStringList{contactDisplayText(c, m_project, true)});
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                setItemType(item, ItemType::Contact);
                item->setData(0, kRoleId, static_cast<qulonglong>(c.id));
                item->setForeground(0, softText());
            applyContactTagDecoration(item, c);
                any_ungrouped = true;
            }
            if (!any_ungrouped && cgrps.empty())
                makeEmptyPlaceholder(sec, tr("No contacts"));
        }
    }
    m_rebuilding = false;
    applyFilter(m_search->text());
}

void LineListPanel::refreshFeatures()
{
    if (!m_project) return;

    QTreeWidgetItem* sec = nullptr;
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        if (itemTypeOf(*it) == ItemType::FeaturesSection) { sec = *it; break; }
        ++it;
    }
    if (!sec) { refresh(); return; }

    m_rebuilding = true;
    {
        QSignalBlocker sb(m_tree);
        while (sec->childCount())
            delete sec->takeChild(0);

        const auto& features = m_project->features();
        if (features.empty()) {
            makeEmptyPlaceholder(sec, tr("No features yet"));
        } else {
            for (const auto& f : features) {
                auto* item = new QTreeWidgetItem(sec, QStringList{featureDisplayText(f)});
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                setItemType(item, ItemType::Feature);
                item->setData(0, kRoleId, static_cast<qulonglong>(f.id));
                item->setForeground(0, softText());
            }
        }
    }
    m_rebuilding = false;
    applyFilter(m_search->text());
}

void LineListPanel::refreshLayer(const std::string& id)
{
    if (!m_project) return;
    const auto* layer = m_project->findLayer(id);
    if (!layer) { refresh(); return; }

    const QString qid = QString::fromStdString(id);
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        if (itemTypeOf(*it) == ItemType::Layer
                && (*it)->data(0, kRoleId).toString() == qid) {
            auto* item = *it;
            QSignalBlocker sb(m_tree);

            item->setText(0, layerDisplayText(layer));
            item->setCheckState(0, layer->visible ? Qt::Checked : Qt::Unchecked);
            item->setData(0, kRoleModality, static_cast<int>(layer->modality));

            const bool is_ready = (layer->state == app::LayerState::Ready);
            Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
            if (is_ready) flags |= Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
            item->setFlags(flags);

            applyLayerStateDecoration(item, layer->state);

            // Re-apply the bottom-tracked bold (it can change after auto/manual picks).
            const bool bottom_tracked =
                layer->bottom_track_kind == app::BottomTrackKind::Auto   ||
                layer->bottom_track_kind == app::BottomTrackKind::Manual ||
                layer->bottom_track_kind == app::BottomTrackKind::Mixed;
            QFont lf = item->font(0);
            lf.setBold(bottom_tracked);
            item->setFont(0, lf);

            item->setIcon(0, makeTagIcon(layer->tags));
            if (!layer->tags.empty() && layer->state == app::LayerState::Ready) {
                QStringList tl;
                for (const auto& t : layer->tags) tl << ("#" + QString::fromStdString(t));
                item->setToolTip(0, tl.join("  "));
            }
            return;
        }
        ++it;
    }
    // Item not found — layer was added under a new modality or group, fall back.
    refresh();
}

// -- Layer reorder sync --------------------------------------------------------

void LineListPanel::syncLayerOrderFromTree()
{
    if (!m_project) return;
    std::vector<std::string> ordered;
    ordered.reserve(m_project->layers().size());

    // Recursively collect Layer ids from a container (Modality or LayerGroup).
    // Groups may be nested inside modality buckets, so we recurse into LayerGroup
    // children rather than assuming a fixed two-level structure.
    std::function<void(QTreeWidgetItem*)> scanContainer = [&](QTreeWidgetItem* container) {
        for (int j = 0; j < container->childCount(); ++j) {
            auto* child = container->child(j);
            if (!child) continue;
            const auto ct = itemTypeOf(child);
            if (ct == ItemType::Layer)
                ordered.push_back(child->data(0, kRoleId).toString().toStdString());
            else if (ct == ItemType::LayerGroup)
                scanContainer(child);  // nested group inside modality bucket
        }
    };

    auto scanParent = [&](QTreeWidgetItem* parent) {
        const int n = parent ? parent->childCount() : m_tree->topLevelItemCount();
        for (int i = 0; i < n; ++i) {
            auto* item = parent ? parent->child(i) : m_tree->topLevelItem(i);
            if (!item) continue;
            const auto type = itemTypeOf(item);
            if (type == ItemType::Modality || type == ItemType::LayerGroup)
                scanContainer(item);
        }
    };

    if (m_mode == ContentMode::LayersOnly) {
        scanParent(nullptr);
    } else {
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
            auto* top = m_tree->topLevelItem(i);
            if (top && itemTypeOf(top) == ItemType::Project) {
                scanParent(top);
                break;
            }
        }
    }
    m_project->reorderLayers(ordered);
}

} // namespace dolphin::ui
