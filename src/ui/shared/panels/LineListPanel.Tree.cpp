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

QColor mutedText() { return Theme::textMutedColor(); }
QColor softText()  { return Theme::textSoftColor(); }

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

static QString tagToolTip(const std::vector<std::string>& tags)
{
    QStringList lines;
    for (const auto& tag : tags)
        lines << ("#" + QString::fromStdString(tag));
    return lines.join("  ");
}

// Always assign the empty values too. Otherwise removing the last tag during a
// targeted update leaves the row's previous icon and tooltip behind.
static void applyTagDecoration(QTreeWidgetItem* item,
                               const std::vector<std::string>& tags,
                               bool update_tooltip = true)
{
    item->setIcon(0, makeTagIcon(tags));
    if (update_tooltip)
        item->setToolTip(0, tagToolTip(tags));
}

static QTreeWidgetItem* addGroupItem(QTreeWidgetItem* parent,
                                     const app::ItemGroup& group,
                                     ItemType type,
                                     Qt::ItemFlags flags)
{
    auto* item = new QTreeWidgetItem(
        parent, QStringList{QString::fromStdString(group.name)});
    item->setFlags(flags);
    setItemType(item, type);
    item->setData(0, kRoleGroupId, QString::fromStdString(group.id));
    item->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_DirIcon));
    QFont font = item->font(0);
    font.setBold(true);
    item->setFont(0, font);
    return item;
}

static void applyContactRow(QTreeWidgetItem* item,
                            const core::Contact& contact,
                            app::Project* project)
{
    item->setText(0, contactDisplayText(contact, project, true));
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable
                   | Qt::ItemIsUserCheckable);
    item->setCheckState(0, contact.visible ? Qt::Checked : Qt::Unchecked);
    setItemType(item, ItemType::Contact);
    item->setData(0, kRoleId, static_cast<qulonglong>(contact.id));
    item->setForeground(0, softText());
    applyTagDecoration(item, contact.tags);
}

static QTreeWidgetItem* addContactItem(QTreeWidgetItem* parent,
                                       const core::Contact& contact,
                                       app::Project* project)
{
    auto* item = new QTreeWidgetItem(parent);
    applyContactRow(item, contact, project);
    return item;
}

static void applyFeatureRow(QTreeWidgetItem* item, const core::Feature& feature)
{
    item->setText(0, featureDisplayText(feature));
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable
                   | Qt::ItemIsUserCheckable);
    item->setCheckState(0, feature.visible ? Qt::Checked : Qt::Unchecked);
    setItemType(item, ItemType::Feature);
    item->setData(0, kRoleId, static_cast<qulonglong>(feature.id));
    item->setForeground(0, softText());
    applyTagDecoration(item, feature.tags);
}

static QTreeWidgetItem* addFeatureItem(QTreeWidgetItem* parent,
                                       const core::Feature& feature)
{
    auto* item = new QTreeWidgetItem(parent);
    applyFeatureRow(item, feature);
    return item;
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
    populateContactsSection(sec);
}

void LineListPanel::populateContactsSection(QTreeWidgetItem* sec)
{
    const auto& contacts  = m_project->contacts();
    const auto& cgrps     = m_project->contactGroups();

    if (contacts.empty() && cgrps.empty()) {
        makeEmptyPlaceholder(sec, tr("No contacts"));
        return;
    }

    // User-defined contact groups first
    for (const auto& grp : cgrps) {
        auto* gi = addGroupItem(sec, grp, ItemType::ContactGroup,
                                Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        bool any = false;
        for (const auto& c : contacts) {
            if (c.group_id != grp.id) continue;
            addContactItem(gi, c, m_project);
            any = true;
        }
        if (!any) makeEmptyPlaceholder(gi, tr("Empty group"));
    }

    // Ungrouped contacts
    bool any_ungrouped = false;
    for (const auto& c : contacts) {
        if (!c.group_id.empty()) continue;
        addContactItem(sec, c, m_project);
        any_ungrouped = true;
    }
    if (!any_ungrouped && cgrps.empty())
        makeEmptyPlaceholder(sec, tr("No contacts"));
}

void LineListPanel::buildFeaturesSection(QTreeWidgetItem* parent)
{
    auto* sec = makeSectionItem(m_tree, parent, tr("Features"));
    setItemType(sec, ItemType::FeaturesSection);
    populateFeaturesSection(sec);
}

void LineListPanel::populateFeaturesSection(QTreeWidgetItem* sec)
{
    const auto& features = m_project->features();
    if (features.empty()) {
        makeEmptyPlaceholder(sec, tr("No features yet"));
        return;
    }
    for (const auto& feature : features)
        addFeatureItem(sec, feature);
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

static void applyLayerRow(QTreeWidgetItem* item, const app::DataLayer* layer)
{
    const bool is_ready = (layer->state == app::LayerState::Ready);
    item->setText(0, layerDisplayText(layer));

    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
    if (is_ready)
        flags |= Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
    item->setFlags(flags);
    item->setData(0, kRoleId, QString::fromStdString(layer->id));
    setItemType(item, ItemType::Layer);
    item->setData(0, kRoleModality, static_cast<int>(resolveModality(layer)));
    item->setCheckState(0, layer->visible ? Qt::Checked : Qt::Unchecked);
    applyLayerStateDecoration(item, layer->state);

    const bool bottom_tracked =
        layer->bottom_track_kind == app::BottomTrackKind::Auto   ||
        layer->bottom_track_kind == app::BottomTrackKind::Manual ||
        layer->bottom_track_kind == app::BottomTrackKind::Mixed;
    QFont font = item->font(0);
    font.setBold(bottom_tracked);
    item->setFont(0, font);

    // Non-ready states own the tooltip ("Indexing" / "Import failed"), but
    // tags still contribute their icon. Ready rows use the tag list as tooltip.
    applyTagDecoration(item, layer->tags, is_ready);
}

static void addLayerItem(QTreeWidgetItem* parent, const app::DataLayer* layer)
{
    auto* item = new QTreeWidgetItem(parent);
    applyLayerRow(item, layer);
}

static std::string displayedGroupId(const QTreeWidgetItem* item,
                                    ItemType group_type)
{
    const auto* parent = item ? item->parent() : nullptr;
    if (!parent || itemTypeOf(parent) != group_type)
        return {};
    return parent->data(0, kRoleGroupId).toString().toStdString();
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
        auto* gi = addGroupItem(container, grp, ItemType::LayerGroup,
                                Qt::ItemIsEnabled | Qt::ItemIsDropEnabled
                                    | Qt::ItemIsSelectable);
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
        populateContactsSection(sec);
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
        populateFeaturesSection(sec);
    }
    m_rebuilding = false;
    applyFilter(m_search->text());
}

void LineListPanel::refreshContactRow(uint64_t contact_id)
{
    if (!m_project) return;
    const core::Contact* c = nullptr;
    for (const auto& cc : m_project->contacts())
        if (cc.id == contact_id) { c = &cc; break; }
    if (!c) { refreshContacts(); return; }   // removed / unknown — rebuild

    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        if (itemTypeOf(*it) == ItemType::Contact
                && (*it)->data(0, kRoleId).toULongLong() == contact_id) {
            if (displayedGroupId(*it, ItemType::ContactGroup) != c->group_id) {
                refreshContacts();
                return;
            }
            m_rebuilding = true;
            {
                QSignalBlocker sb(m_tree);
                applyContactRow(*it, *c, m_project);
            }
            m_rebuilding = false;
            return;
        }
        ++it;
    }
    refreshContacts();   // row not found (e.g. group moved) — full rebuild
}

void LineListPanel::refreshFeatureRow(uint64_t feature_id)
{
    if (!m_project) return;
    const core::Feature* f = nullptr;
    for (const auto& ff : m_project->features())
        if (ff.id == feature_id) { f = &ff; break; }
    if (!f) { refreshFeatures(); return; }

    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        if (itemTypeOf(*it) == ItemType::Feature
                && (*it)->data(0, kRoleId).toULongLong() == feature_id) {
            m_rebuilding = true;
            {
                QSignalBlocker sb(m_tree);
                applyFeatureRow(*it, *f);
            }
            m_rebuilding = false;
            return;
        }
        ++it;
    }
    refreshFeatures();
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
            const auto displayed_modality = static_cast<app::Modality>(
                item->data(0, kRoleModality).toInt());
            if (displayedGroupId(item, ItemType::LayerGroup) != layer->group_id
                    || displayed_modality != resolveModality(layer)) {
                refresh();
                return;
            }
            QSignalBlocker sb(m_tree);
            applyLayerRow(item, layer);
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
