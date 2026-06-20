// ContactManagerWindow.View.cpp — nav tree build, list/card population, search,
// breadcrumb, status, preview pane, view-mode switching, and folder navigation.
#include "ui/features/contacts/ContactManagerWindow.h"
#include "ui/features/contacts/ContactVisuals.h"
#include "ui/shell/Theme.h"
#include "ui/shared/CoordFormat.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "app/layers/LayerUtils.h"
#include "core/SpatialRef.h"

#include <QColor>
#include <QDateTime>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QLineEdit>
#include <QListWidget>
#include <QStackedWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>

#include <algorithm>
#include <functional>
#include <map>

namespace dolphin::ui {

using namespace dolphin::ui::cmvis;

// -- Folder navigation --------------------------------------------------------

void ContactManagerWindow::navBack()
{
    if (m_nav_pos <= 0) return;
    --m_nav_pos;
    m_nav_replaying = true;
    if (auto* it = findNavItem(m_nav_history[m_nav_pos].kind,
                               m_nav_history[m_nav_pos].mod,
                               m_nav_history[m_nav_pos].line))
        m_nav->setCurrentItem(it);
    m_nav_replaying = false;
    updateNavButtons();
}

void ContactManagerWindow::navForward()
{
    if (m_nav_pos + 1 >= static_cast<int>(m_nav_history.size())) return;
    ++m_nav_pos;
    m_nav_replaying = true;
    if (auto* it = findNavItem(m_nav_history[m_nav_pos].kind,
                               m_nav_history[m_nav_pos].mod,
                               m_nav_history[m_nav_pos].line))
        m_nav->setCurrentItem(it);
    m_nav_replaying = false;
    updateNavButtons();
}

void ContactManagerWindow::navUp()
{
    if (auto* cur = m_nav->currentItem())
        if (auto* parent = cur->parent())
            m_nav->setCurrentItem(parent);
}

void ContactManagerWindow::recordNavLocation()
{
    if (m_nav_replaying) return;
    auto* cur = m_nav->currentItem();
    if (!cur) return;
    NavLoc loc{ cur->data(0, RoleKind).toInt(),
                cur->data(0, RoleModality).toInt(),
                cur->data(0, RoleLineId).toString() };
    // Skip if it's the folder we're already on (e.g. re-selected after a refresh).
    if (m_nav_pos >= 0 && m_nav_pos < static_cast<int>(m_nav_history.size())) {
        const NavLoc& top = m_nav_history[m_nav_pos];
        if (top.kind == loc.kind && top.mod == loc.mod && top.line == loc.line) return;
    }
    // Drop any forward history, then append.
    if (m_nav_pos + 1 < static_cast<int>(m_nav_history.size()))
        m_nav_history.resize(m_nav_pos + 1);
    m_nav_history.push_back(loc);
    m_nav_pos = static_cast<int>(m_nav_history.size()) - 1;
    updateNavButtons();
}

void ContactManagerWindow::updateNavButtons()
{
    if (m_act_back) m_act_back->setEnabled(m_nav_pos > 0);
    if (m_act_fwd)  m_act_fwd->setEnabled(m_nav_pos + 1 < static_cast<int>(m_nav_history.size()));
    if (m_act_up) {
        auto* cur = m_nav->currentItem();
        m_act_up->setEnabled(cur && cur->parent() != nullptr);
    }
}

QTreeWidgetItem* ContactManagerWindow::findNavItem(int kind, int mod, const QString& line) const
{
    std::function<QTreeWidgetItem*(QTreeWidgetItem*)> walk =
        [&](QTreeWidgetItem* it) -> QTreeWidgetItem* {
            if (it->data(0, RoleKind).toInt() == kind
                && it->data(0, RoleModality).toInt() == mod
                && it->data(0, RoleLineId).toString() == line)
                return it;
            for (int i = 0; i < it->childCount(); ++i)
                if (auto* hit = walk(it->child(i))) return hit;
            return nullptr;
        };
    for (int i = 0; i < m_nav->topLevelItemCount(); ++i)
        if (auto* hit = walk(m_nav->topLevelItem(i))) return hit;
    return nullptr;
}

// -- Navigation tree ----------------------------------------------------------

void ContactManagerWindow::rebuildNav()
{
    int     sel_kind   = NavAll;
    int     sel_mod    = -1;
    int     sel_facet  = -1;
    QString sel_line;
    QString sel_bucket;
    QString sel_group;
    bool    sel_group_set = false;
    if (auto* cur = m_nav->currentItem()) {
        sel_kind   = cur->data(0, RoleKind).toInt();
        sel_mod    = cur->data(0, RoleModality).toInt();
        sel_facet  = cur->data(0, RoleFacet).toInt();
        sel_line   = cur->data(0, RoleLineId).toString();
        sel_bucket = cur->data(0, RoleBucketKey).toString();
        if (sel_kind == NavGroup) { sel_group = cur->data(0, RoleGroupId).toString(); sel_group_set = true; }
    }

    m_nav->blockSignals(true);
    m_nav->clear();
    const QIcon folder_icon(QStringLiteral(":/icons/contacts.svg"));

    auto* all = new QTreeWidgetItem(m_nav);
    all->setData(0, RoleKind, NavAll);
    all->setIcon(0, QIcon(QStringLiteral(":/icons/database.svg")));
    QTreeWidgetItem* to_select = all;

    // Build the section list unconditionally so the facet categories are always
    // visible on the left (even with no project open / no contacts yet).
    static const std::vector<core::Contact> kNoContacts;
    const std::vector<core::Contact>& contacts = m_project ? m_project->contacts() : kNoContacts;
    const int total = static_cast<int>(contacts.size());
    all->setText(0, tr("All Contacts (%1)").arg(total));

    {
        // ★ Favourites quick-access.
        int fav_count = 0;
        for (const auto& c : contacts) if (isFavourite(c)) ++fav_count;
        if (fav_count > 0) {
            auto* fav = new QTreeWidgetItem(m_nav);
            fav->setText(0, tr("★ Favourites (%1)").arg(fav_count));
            fav->setData(0, RoleKind, NavFav);
            if (sel_kind == NavFav) to_select = fav;
        }

        // A non-selectable section header, listed vertically under All Contacts.
        auto makeHeader = [&](int facet, const QString& name, int n_buckets) {
            auto* h = new QTreeWidgetItem(m_nav);
            h->setText(0, QStringLiteral("%1 (%2)").arg(name).arg(n_buckets));
            h->setData(0, RoleKind, NavFacetHeader);
            h->setData(0, RoleFacet, facet);
            h->setFlags(Qt::ItemIsEnabled);   // expandable but not selectable
            QFont hf = m_nav->font(); hf.setBold(true);
            h->setFont(0, hf);
            h->setForeground(0, QColor(Theme::kTextMuted));
            return h;
        };

        // -- Sensor section (SSS/SBP/MAG/MBES → line, + Map) ------------------
        {
            std::map<int, int>                        mod_counts;
            std::map<int, std::map<std::string, int>> line_counts;
            std::map<std::string, std::string>        line_labels;
            int map_count = 0;
            for (const auto& c : contacts) {
                app::DataLayer* layer = c.line_id.empty() ? nullptr : m_project->findLayer(c.line_id);
                if (!layer) { ++map_count; continue; }
                const int mi = static_cast<int>(layer->modality);
                ++mod_counts[mi];
                ++line_counts[mi][c.line_id];
                line_labels[c.line_id] = layer->label;
            }
            const int n = static_cast<int>(mod_counts.size())
                        + ((m_show_map_folder && map_count > 0) ? 1 : 0);
            auto* h = makeHeader(GroupSensor, tr("Sensor"), n);
            for (app::Modality m : kSensorOrder) {
                const int mi = static_cast<int>(m);
                auto it = mod_counts.find(mi);
                if (it == mod_counts.end()) continue;

                auto* mod_node = new QTreeWidgetItem(h);
                mod_node->setText(0, QStringLiteral("%1 (%2)").arg(sensorFolderLabel(m)).arg(it->second));
                mod_node->setIcon(0, folder_icon);
                mod_node->setData(0, RoleKind, NavModality);
                mod_node->setData(0, RoleModality, mi);
                if (sel_kind == NavModality && sel_mod == mi) to_select = mod_node;

                for (const auto& [line_id, cnt] : line_counts[mi]) {
                    auto* line_node = new QTreeWidgetItem(mod_node);
                    line_node->setText(0, QStringLiteral("%1 (%2)")
                        .arg(QString::fromStdString(line_labels[line_id])).arg(cnt));
                    line_node->setData(0, RoleKind, NavLine);
                    line_node->setData(0, RoleModality, mi);
                    line_node->setData(0, RoleLineId, QString::fromStdString(line_id));
                    if (sel_kind == NavLine && sel_line == QString::fromStdString(line_id))
                        to_select = line_node;
                }
            }
            if (m_show_map_folder && map_count > 0) {
                auto* map_node = new QTreeWidgetItem(h);
                map_node->setText(0, tr("Map / unlinked (%1)").arg(map_count));
                map_node->setIcon(0, folder_icon);
                map_node->setData(0, RoleKind, NavMap);
                if (sel_kind == NavMap) to_select = map_node;
            }
            h->setExpanded(m_expanded_facets.count(GroupSensor) != 0);
        }

        // -- Flat facet sections: Class / Confidence / Group / Date / Label --
        struct Facet { int id; QString name; };
        const Facet facets[] = {
            { GroupClass,      tr("Class")      },
            { GroupConfidence, tr("Confidence") },
            { GroupGroup,      tr("Group")      },
            { GroupDate,       tr("Date")       },
            { GroupLabel,      tr("Label")      },
        };
        for (const auto& f : facets) {
            // -- Group: user-defined groups (+ Ungrouped), managed by the user --
            if (f.id == GroupGroup) {
                std::map<std::string, int> gcounts;   // group_id -> count
                int ungrouped = 0;
                for (const auto& c : contacts) {
                    if (!c.group_id.empty() && m_project && m_project->findContactGroup(c.group_id))
                        ++gcounts[c.group_id];
                    else
                        ++ungrouped;
                }
                const int n_groups = m_project ? static_cast<int>(m_project->contactGroups().size()) : 0;
                auto* h = makeHeader(GroupGroup, f.name, n_groups + 1);
                if (m_project) {
                    for (const auto& g : m_project->contactGroups()) {
                        auto* node = new QTreeWidgetItem(h);
                        const int cnt = gcounts.count(g.id) ? gcounts[g.id] : 0;
                        node->setText(0, QStringLiteral("%1 (%2)")
                            .arg(QString::fromStdString(g.name)).arg(cnt));
                        node->setIcon(0, folder_icon);
                        node->setData(0, RoleKind, NavGroup);
                        node->setData(0, RoleGroupId, QString::fromStdString(g.id));
                        if (sel_group_set && sel_group == QString::fromStdString(g.id)) to_select = node;
                    }
                }
                auto* ung = new QTreeWidgetItem(h);
                ung->setText(0, tr("Ungrouped (%1)").arg(ungrouped));
                ung->setData(0, RoleKind, NavGroup);
                ung->setData(0, RoleGroupId, QString());
                if (sel_group_set && sel_group.isEmpty()) to_select = ung;
                h->setExpanded(m_expanded_facets.count(GroupGroup) != 0);
                continue;
            }

            std::map<QString, int> counts;
            for (const auto& c : contacts) ++counts[bucketKey(f.id, c, m_project)];

            std::vector<std::pair<QString, int>> buckets(counts.begin(), counts.end());
            if (f.id == GroupDate) {
                std::sort(buckets.begin(), buckets.end(),
                          [](const auto& a, const auto& b) { return a.first > b.first; });
            } else if (f.id == GroupConfidence) {
                std::sort(buckets.begin(), buckets.end(),
                          [](const auto& a, const auto& b) {
                              return confidenceRank(a.first) < confidenceRank(b.first); });
            } else {
                std::sort(buckets.begin(), buckets.end(),
                          [](const auto& a, const auto& b) {
                              return a.first.localeAwareCompare(b.first) < 0; });
            }

            auto* h = makeHeader(f.id, f.name, static_cast<int>(buckets.size()));
            for (const auto& [key, n] : buckets) {
                auto* node = new QTreeWidgetItem(h);
                node->setText(0, QStringLiteral("%1 (%2)").arg(key).arg(n));
                node->setIcon(0, folder_icon);
                node->setData(0, RoleKind, NavBucket);
                node->setData(0, RoleFacet, f.id);
                node->setData(0, RoleBucketKey, key);
                if (sel_kind == NavBucket && sel_facet == f.id && sel_bucket == key)
                    to_select = node;
            }
            h->setExpanded(m_expanded_facets.count(f.id) != 0);
        }

        // -- Recycle Bin (soft-deleted contacts) -----------------------------
        const int recycled = m_project ? static_cast<int>(m_project->recycledContacts().size()) : 0;
        auto* bin = new QTreeWidgetItem(m_nav);
        bin->setText(0, tr("Recycle Bin (%1)").arg(recycled));
        bin->setIcon(0, QIcon(QStringLiteral(":/icons/recycle_bin.svg")));
        bin->setData(0, RoleKind, NavRecycle);
        if (sel_kind == NavRecycle) to_select = bin;
    }

    m_nav->blockSignals(false);
    m_nav->setCurrentItem(to_select);
    if (m_act_export) m_act_export->setEnabled(total > 0);
}

// -- Details + thumbnail population -------------------------------------------

void ContactManagerWindow::populateForCurrentNode()
{
    const bool was_sorting = m_table->isSortingEnabled();
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    m_thumbs->clear();

    auto* node = m_nav->currentItem();
    const int     kind   = node ? node->data(0, RoleKind).toInt() : NavAll;
    const int     mod    = node ? node->data(0, RoleModality).toInt() : -1;
    const int     facet  = node ? node->data(0, RoleFacet).toInt() : -1;
    const QString line   = node ? node->data(0, RoleLineId).toString() : QString();
    const QString bucket = node ? node->data(0, RoleBucketKey).toString() : QString();
    const std::string grp = node ? node->data(0, RoleGroupId).toString().toStdString() : std::string();

    if (m_project) {
        const auto& source = (kind == NavRecycle) ? m_project->recycledContacts()
                                                  : m_project->contacts();
        for (const auto& c : source) {
            app::DataLayer* layer = c.line_id.empty() ? nullptr : m_project->findLayer(c.line_id);
            const int  c_mod    = layer ? static_cast<int>(layer->modality) : -1;
            const bool unlinked = (layer == nullptr);

            bool include = true;
            switch (kind) {
            case NavRecycle:  include = true;                                           break;
            case NavFav:      include = isFavourite(c);                                  break;
            case NavModality: include = (!unlinked && c_mod == mod);                     break;
            case NavMap:      include = unlinked;                                        break;
            case NavLine:     include = (QString::fromStdString(c.line_id) == line);     break;
            case NavBucket:   include = (bucketKey(facet, c, m_project) == bucket);      break;
            case NavGroup: {
                const bool valid = !c.group_id.empty() && m_project
                                   && m_project->findContactGroup(c.group_id);
                include = grp.empty() ? !valid : (c.group_id == grp);
                break;
            }
            case NavAll:
            default:          include = true;                                           break;
            }
            if (!include) continue;

            const QString tag = layer ? modalityTag(layer->modality) : tr("Map");
            const QString source = layer ? QString::fromStdString(layer->label)
                                         : (c.line_id.empty() ? tr("(map pick)")
                                                              : QString::fromStdString(c.line_id));
            const QString star = isFavourite(c) ? QStringLiteral("★ ") : QString();

            const int row = m_table->rowCount();
            m_table->insertRow(row);
            auto* label_item = new QTableWidgetItem(star + QString::fromStdString(c.label));
            label_item->setData(Qt::UserRole, static_cast<qulonglong>(c.id));
            m_table->setItem(row, ColLabel, label_item);
            m_table->setItem(row, ColSensor, new QTableWidgetItem(tag));
            m_table->setItem(row, ColSource, new QTableWidgetItem(source));
            m_table->setItem(row, ColClass,  new QTableWidgetItem(QString::fromStdString(c.classification)));
            m_table->setItem(row, ColConf,   new QTableWidgetItem(confidenceLabel(c.confidence)));
            const bool proj = core::spatialRefIsProjected(c.spatial_ref);
            m_table->setItem(row, ColLat, new QTableWidgetItem(formatCoord(c.lat, proj, 'N', 'S')));
            m_table->setItem(row, ColLon, new QTableWidgetItem(formatCoord(c.lon, proj, 'E', 'W')));
            auto* depth = new QTableWidgetItem(
                c.depth_m > 0.f ? QString::number(c.depth_m, 'f', 1) : QStringLiteral("—"));
            depth->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(row, ColDepth, depth);
            auto* range = new QTableWidgetItem(
                c.range_m > 0.f ? QString::number(c.range_m, 'f', 1) : QStringLiteral("—"));
            range->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(row, ColRange, range);

            auto* thumb = new QListWidgetItem(m_thumbs);
            thumb->setIcon(QIcon(contactThumbnail(m_project, c, 84)));
            thumb->setText(star + QString::fromStdString(c.label));
            thumb->setData(Qt::UserRole, static_cast<qulonglong>(c.id));
            thumb->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
            thumb->setToolTip(tr("%1\n%2 · %3").arg(QString::fromStdString(c.label), tag, source));
        }
    }

    m_table->setSortingEnabled(was_sorting);
    applySearch();
}

void ContactManagerWindow::applySearch()
{
    const QString needle = m_search ? m_search->text().trimmed() : QString();
    const auto* contacts = m_project ? &m_project->contacts() : nullptr;

    auto matches = [&](uint64_t id, const QString& label, const QString& cls) -> bool {
        if (needle.isEmpty()) return true;
        QString hay = label + QLatin1Char(' ') + cls;
        if (contacts)
            for (const auto& c : *contacts)
                if (c.id == id) { hay += QLatin1Char(' ') + QString::fromStdString(c.notes); break; }
        return hay.contains(needle, Qt::CaseInsensitive);
    };

    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto* li = m_table->item(row, ColLabel);
        if (!li) continue;
        const QString cls = m_table->item(row, ColClass) ? m_table->item(row, ColClass)->text() : QString();
        m_table->setRowHidden(row, !matches(li->data(Qt::UserRole).toULongLong(), li->text(), cls));
    }
    for (int i = 0; i < m_thumbs->count(); ++i) {
        auto* it = m_thumbs->item(i);
        it->setHidden(!matches(it->data(Qt::UserRole).toULongLong(), it->text(), QString()));
    }
    updateStatus();
}

void ContactManagerWindow::updateBreadcrumb()
{
    if (!m_breadcrumb) return;
    m_crumb_items.clear();
    for (auto* it = m_nav->currentItem(); it; it = it->parent())
        m_crumb_items.prepend(it);

    QStringList html;
    for (int i = 0; i < m_crumb_items.size(); ++i) {
        QString t = m_crumb_items[i]->text(0);
        const int paren = t.lastIndexOf(QLatin1String(" ("));
        if (paren > 0) t = t.left(paren);
        t = t.toHtmlEscaped();
        if (i == m_crumb_items.size() - 1) html << QStringLiteral("<b>%1</b>").arg(t);
        else                               html << QStringLiteral("<a href=\"%1\">%2</a>").arg(i).arg(t);
    }
    if (html.isEmpty()) html << tr("Contacts");
    m_breadcrumb->setText(html.join(QStringLiteral("&nbsp;&nbsp;›&nbsp;&nbsp;")));
}

void ContactManagerWindow::updateCommandState()
{
    const auto ids = selectedIds();
    const bool has = !ids.empty();
    const bool bin = isViewingRecycleBin();   // editing commands don't apply in the bin
    if (m_act_cut)    m_act_cut->setEnabled(has && !bin);
    if (m_act_copy)   m_act_copy->setEnabled(has);
    if (m_act_delete) m_act_delete->setEnabled(has);   // = "Delete Forever" in the bin
    if (m_act_fav)    m_act_fav->setEnabled(has && !bin);
    if (m_act_rename) m_act_rename->setEnabled(ids.size() == 1 && !bin);
    if (m_act_props)  m_act_props->setEnabled(ids.size() == 1);
    if (m_act_paste)  m_act_paste->setEnabled(!m_clipboard.empty() && !bin);
}

void ContactManagerWindow::updateStatus()
{
    if (!m_status) return;
    int shown = 0;
    if (m_view_mode == 0) {
        for (int row = 0; row < m_table->rowCount(); ++row)
            if (!m_table->isRowHidden(row)) ++shown;
    } else {
        for (int i = 0; i < m_thumbs->count(); ++i)
            if (!m_thumbs->item(i)->isHidden()) ++shown;
    }
    const int sel = static_cast<int>(selectedIds().size());
    QString t = tr("%n contact(s)", nullptr, shown);
    if (sel > 0) t += tr("   ·   %1 selected").arg(sel);
    m_status->setText(t);
}

void ContactManagerWindow::setViewMode(int mode)
{
    if (m_btn_details)      m_btn_details->setChecked(mode == 0);
    if (m_btn_thumbs)       m_btn_thumbs->setChecked(mode == 1);
    if (m_view_details_act) m_view_details_act->setChecked(mode == 0);
    if (m_view_thumbs_act)  m_view_thumbs_act->setChecked(mode == 1);
    if (mode == m_view_mode) return;

    const auto keep = selectedIds();
    m_view_mode = mode;
    m_views->setCurrentIndex(mode);

    if (mode == 1) {
        for (int i = 0; i < m_thumbs->count(); ++i) {
            const uint64_t id = m_thumbs->item(i)->data(Qt::UserRole).toULongLong();
            m_thumbs->item(i)->setSelected(std::find(keep.begin(), keep.end(), id) != keep.end());
        }
    } else {
        m_table->clearSelection();
        for (int row = 0; row < m_table->rowCount(); ++row) {
            auto* it = m_table->item(row, ColLabel);
            if (it && std::find(keep.begin(), keep.end(),
                                it->data(Qt::UserRole).toULongLong()) != keep.end())
                m_table->selectRow(row);
        }
    }
    updateCommandState();
    updateStatus();
    updatePreview();
}

void ContactManagerWindow::updatePreview()
{
    const auto ids = selectedIds();
    if (ids.size() != 1) {
        if (auto* empty = qobject_cast<QLabel*>(m_pv_stack->widget(0)))
            empty->setText(ids.empty() ? tr("Select a contact to see details")
                                       : tr("%1 contacts selected").arg(ids.size()));
        m_pv_stack->setCurrentIndex(0);
        return;
    }
    const auto* c = findContact(ids.front());
    if (!c) { m_pv_stack->setCurrentIndex(0); return; }

    app::DataLayer* layer = c->line_id.empty() ? nullptr : m_project->findLayer(c->line_id);
    const QString tag = layer ? modalityTag(layer->modality) : tr("Map");
    const QString src = layer ? QString::fromStdString(layer->label)
                              : (c->line_id.empty() ? tr("(map pick)")
                                                    : QString::fromStdString(c->line_id));
    const QString cf  = confidenceLabel(c->confidence);

    m_pv_title->setText((isFavourite(*c) ? QStringLiteral("★  ") : QString())
                        + QString::fromStdString(c->label));

    // Snapshot grabbed when the contact was picked (hidden when none exists).
    if (m_pv_image) {
        const QString snap_path = contactSnapshotPath(m_project, c->id);
        QPixmap snap;
        if (!snap_path.isEmpty() && QFileInfo::exists(snap_path)) snap.load(snap_path);
        if (!snap.isNull()) {
            m_pv_image->setPixmap(snap.scaled(m_pv_image->width(), m_pv_image->height(),
                                              Qt::KeepAspectRatio, Qt::SmoothTransformation));
            m_pv_image->setVisible(true);
        } else {
            m_pv_image->clear();
            m_pv_image->setVisible(false);
        }
    }

    auto chipCss = [](const QColor& col) {
        return QStringLiteral("background:rgba(%1,%2,%3,0.15); color:%4;"
                              "border:1px solid rgba(%1,%2,%3,0.5); border-radius:9px;"
                              "padding:1px 9px; font-size:10px; font-weight:700;")
            .arg(col.red()).arg(col.green()).arg(col.blue()).arg(col.name());
    };
    m_pv_sensor->setText(tag);
    m_pv_sensor->setStyleSheet(chipCss(sensorColor(tag)));
    m_pv_conf->setText(cf);
    m_pv_conf->setStyleSheet(chipCss(confidenceColor(cf)));

    m_pv_class->setText(c->classification.empty() ? tr("—") : QString::fromStdString(c->classification));
    m_pv_line->setText(src);
    const bool proj = core::spatialRefIsProjected(c->spatial_ref);
    m_pv_coords->setText(formatPosition(c->lat, c->lon, proj));
    m_pv_depth->setText(c->depth_m > 0.f ? tr("%1 m").arg(c->depth_m, 0, 'f', 1) : tr("—"));
    m_pv_range->setText(c->range_m > 0.f ? tr("%1 m").arg(c->range_m, 0, 'f', 1) : tr("—"));
    m_pv_notes->setText(c->notes.empty() ? tr("—") : QString::fromStdString(c->notes));

    QStringList meta;
    if (c->created_at > 0.0)
        meta << tr("Created %1").arg(
            QDateTime::fromSecsSinceEpoch(static_cast<qint64>(c->created_at)).toString("yyyy-MM-dd hh:mm"));
    if (c->modified_at > 0.0)
        meta << tr("Modified %1").arg(
            QDateTime::fromSecsSinceEpoch(static_cast<qint64>(c->modified_at)).toString("yyyy-MM-dd hh:mm"));
    if (!c->tags.empty()) {
        QStringList tags;
        for (const auto& t : c->tags) tags << QString::fromStdString(t);
        meta << tr("Tags: %1").arg(tags.join(QStringLiteral(", ")));
    }
    m_pv_meta->setText(meta.join(QStringLiteral("\n")));

    m_pv_stack->setCurrentIndex(1);
}

} // namespace dolphin::ui
