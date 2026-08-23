// ContactEditorDialog.State.cpp — contact loading, form state, and navigation.

#include "ui/features/contacts/ContactEditorDialog.h"
#include "ui/features/contacts/ContactSnapshotView.h"
#include "ui/features/contacts/ContactVisuals.h"
#include "ui/shared/CoordFormat.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "core/NavPoint.h"
#include "core/SpatialRef.h"
#include "geo/GeoUtils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QStringList>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <utility>

namespace dolphin::ui {

using cmvis::contactSnapshotPath;

namespace {

// Editable fields the form owns — used to decide whether a commit is needed.
bool editableEqual(const core::Contact& a, const core::Contact& b)
{
    auto fe = [](float x, float y) { return std::fabs(x - y) < 1e-4f; };
    // Coordinate tolerance = half the spin-box resolution (0.01 m projected,
    // 1e-6° geographic) so the display rounding never registers as an edit.
    const double ceps = core::spatialRefIsProjected(a.spatial_ref) ? 5e-3 : 5e-7;
    auto de = [ceps](double x, double y) { return std::fabs(x - y) < ceps; };
    return a.label == b.label
        && de(a.lat, b.lat)
        && de(a.lon, b.lon)
        && a.symbol == b.symbol
        && a.color_rgb == b.color_rgb
        && a.classification == b.classification
        && a.confidence == b.confidence
        && fe(a.height_m, b.height_m)
        && a.height_not_measurable == b.height_not_measurable
        && fe(a.shadow_m, b.shadow_m)
        && fe(a.width_m, b.width_m)
        && fe(a.length_m, b.length_m)
        && fe(a.depth_m, b.depth_m)
        && fe(a.burial_depth_m, b.burial_depth_m)
        && a.notes == b.notes
        && a.use_for_report == b.use_for_report
        && a.tags == b.tags;
}

} // namespace

void ContactEditorDialog::setSnapshotProvider(
    std::function<ContactSnapshotData(const core::Contact&)> fn)
{
    m_snapshot_provider = std::move(fn);
    // The constructor loads the first contact before the owner installs the
    // provider — fetch now if that load came up empty.
    if (m_snapshot_provider && m_snap && m_index >= 0)
        if (const core::Contact* c = findContact(currentId()))
            loadContactIntoForm(*c);
}

// ---------------------------------------------------------------------------
//  Data flow
// ---------------------------------------------------------------------------

const core::Contact* ContactEditorDialog::findContact(uint64_t id) const
{
    if (!m_project) return nullptr;
    for (const auto& c : m_project->contacts())
        if (c.id == id) return &c;
    return nullptr;
}

uint64_t ContactEditorDialog::currentId() const
{
    return (m_index >= 0 && m_index < static_cast<int>(m_ids.size()))
         ? m_ids[m_index] : 0;
}

QColor ContactEditorDialog::effectiveColor() const
{
    return m_color.isValid() ? m_color : QColor(255, 64, 64);
}

void ContactEditorDialog::setColorSwatch(const QColor& c)
{
    m_color = c;
    const QColor sw = effectiveColor();
    // Pure swatch: the chip fills the button; the value lives in the tooltip.
    QPixmap chip(42, 14);
    chip.fill(sw);
    m_color_btn->setText(QString());
    m_color_btn->setIcon(QIcon(chip));
    m_color_btn->setIconSize(chip.size());
    m_color_btn->setToolTip(m_color.isValid()
        ? tr("Contact colour %1 — click to change.")
              .arg(sw.name(QColor::HexRgb).toUpper())
        : tr("Contact colour: automatic (per classification) — click to choose."));
    if (!m_loading && m_snap) m_snap->setMarkerColor(sw);
}

void ContactEditorDialog::updateCoordEchoes()
{
    const bool proj = core::spatialRefIsProjected(m_before.spatial_ref);
    const double n = m_coord_n->value();
    const double e = m_coord_e->value();

    QString echo_n, echo_e, footer;
    if (proj) {
        // Try the projected → WGS84 transform (UTM-family CRSs); otherwise show
        // the CRS name so the operator still knows what the numbers mean.
        core::NavPoint in;
        in.lat          = n;
        in.lon          = e;
        in.is_projected = true;
        in.spatial_ref  = m_before.spatial_ref;
        in.valid        = true;
        core::NavPoint out;
        if (geo::normalizeNavForMap(in, core::makeWgs84SpatialRef(), out)) {
            echo_n = formatCoord(out.lat, false, 'N', 'S');
            echo_e = formatCoord(out.lon, false, 'E', 'W');
            footer = QStringLiteral("%1, %2 E (m)  ·  %3  %4")
                         .arg(n, 0, 'f', 2).arg(e, 0, 'f', 2)
                         .arg(echo_n, echo_e);
        } else {
            echo_n = spatialRefDisplayName(m_before.spatial_ref);
            footer = formatPosition(n, e, true);
        }
    } else {
        footer = formatPosition(n, e, false);
    }
    m_coord_n_echo->setText(echo_n);
    m_coord_e_echo->setText(echo_e);
    if (m_img_coords) m_img_coords->setText(footer);
}

void ContactEditorDialog::loadIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_ids.size())) return;
    if (index != m_index) commitIfChanged();

    m_index = index;
    const core::Contact* c = findContact(m_ids[index]);
    if (!c) { updateNavState(); return; }

    m_before = *c;
    loadContactIntoForm(*c);
    updateNavState();
    emit contactActivated(c->id);
}

void ContactEditorDialog::loadContactIntoForm(const core::Contact& c)
{
    m_loading = true;

    m_name->setText(QString::fromStdString(c.label));

    const int sym_idx = m_symbol->findData(QString::fromStdString(c.symbol));
    m_symbol->setCurrentIndex(sym_idx >= 0 ? sym_idx : 0);

    m_class->setCurrentText(QString::fromStdString(c.classification));

    m_color = (c.color_rgb != 0) ? QColor::fromRgba(c.color_rgb) : QColor();
    setColorSwatch(m_color);

    // Position: reconfigure the editable rows for the contact's CRS.
    const bool proj = core::spatialRefIsProjected(c.spatial_ref);
    if (proj) {
        m_coord_n_label->setText(tr("Northing:"));
        m_coord_e_label->setText(tr("Easting:"));
        m_coord_n->setRange(-1e8, 1e8);  m_coord_n->setDecimals(2);
        m_coord_e->setRange(-1e8, 1e8);  m_coord_e->setDecimals(2);
        m_coord_n->setSuffix(QStringLiteral(" m"));
        m_coord_e->setSuffix(QStringLiteral(" m"));
        m_coord_n->setSingleStep(1.0);
        m_coord_e->setSingleStep(1.0);
    } else {
        m_coord_n_label->setText(tr("Latitude:"));
        m_coord_e_label->setText(tr("Longitude:"));
        m_coord_n->setRange(-90.0, 90.0);    m_coord_n->setDecimals(6);
        m_coord_e->setRange(-180.0, 180.0);  m_coord_e->setDecimals(6);
        m_coord_n->setSuffix(QStringLiteral("°"));
        m_coord_e->setSuffix(QStringLiteral("°"));
        m_coord_n->setSingleStep(0.0001);
        m_coord_e->setSingleStep(0.0001);
    }
    m_coord_n->setValue(c.lat);   // northing == lat slot, easting == lon slot
    m_coord_e->setValue(c.lon);
    updateCoordEchoes();

    m_height->setValue(static_cast<double>(c.height_m));
    m_height_nm->setChecked(c.height_not_measurable);
    m_height->setEnabled(!c.height_not_measurable);
    m_shadow->setValue(static_cast<double>(c.shadow_m));
    m_width->setValue(static_cast<double>(c.width_m));
    m_length->setValue(static_cast<double>(c.length_m));
    m_depth->setValue(static_cast<double>(c.depth_m));
    m_burial->setValue(static_cast<double>(c.burial_depth_m));

    m_confidence->setCurrentIndex(static_cast<int>(c.confidence));

    // Tags: this contact's tags in the list; project-wide tags as suggestions.
    m_tags_list->clear();
    for (const auto& t : c.tags)
        m_tags_list->addItem(QString::fromStdString(t));
    m_tags_combo->clear();
    if (m_project) {
        QStringList suggestions;
        for (const auto& pc : m_project->contacts())
            for (const auto& t : pc.tags) {
                const QString qt = QString::fromStdString(t);
                if (!suggestions.contains(qt)) suggestions << qt;
            }
        suggestions.sort(Qt::CaseInsensitive);
        m_tags_combo->addItems(suggestions);
    }
    m_tags_combo->clearEditText();

    m_desc->setPlainText(QString::fromStdString(c.notes));
    m_use_report->setChecked(c.use_for_report);

    // Source caption: keep this to the operator-facing line name. Channel and
    // pick metadata belong to the image itself, not this compact selector.
    QString line_name;
    if (m_project && !c.line_id.empty()) {
        if (auto* layer = m_project->findLayer(c.line_id)) {
            line_name = QString::fromStdString(layer->label).trimmed();
            if (line_name.isEmpty()) {
                if (const auto* src = m_project->findSource(layer->source_id))
                    line_name = QFileInfo(QString::fromStdString(src->path)).completeBaseName();
            }
        }
    }
    m_source_combo->clear();
    m_source_combo->addItem(!line_name.isEmpty() ? line_name : tr("Source image"));

    // Snapshot: persisted PNG first; otherwise fetch from the source pings.
    QPixmap pm;
    const QString path = contactSnapshotPath(m_project, c.id);
    if (!path.isEmpty()) pm.load(path);
    m_measure_across_m_per_px = c.snapshot_across_m_per_px;
    m_measure_along_m_per_px  = c.snapshot_along_m_per_px;
    m_measure_altitude_m      = c.pick_altitude_m;
    if (m_snapshot_provider && (pm.isNull()
            || m_measure_across_m_per_px <= 0.f
            || m_measure_along_m_per_px <= 0.f)) {
        const ContactSnapshotData recovered = m_snapshot_provider(c);
        if (recovered.calibrated()) {
            pm = recovered.pixmap;
            m_measure_across_m_per_px = recovered.across_m_per_px;
            m_measure_along_m_per_px  = recovered.along_m_per_px;
            if (recovered.altitude_m > 0.f)
                m_measure_altitude_m = recovered.altitude_m;
        } else if (pm.isNull() && !recovered.pixmap.isNull()) {
            pm = recovered.pixmap;
        }
    }
    m_snap->setPixmap(pm);
    m_snap->setMeasurementScale(m_measure_across_m_per_px,
                                m_measure_along_m_per_px);
    m_snap->setContactSide(c.range_m > 0.f ? (c.sample_idx == 0 ? -1 : 1) : 0);
    const bool calibrated = m_snap->hasMeasurementScale();
    m_snap->setMeasurementMode(ContactSnapshotView::NoMeasurement);
    const QString measurement_tip = calibrated
        ? tr("Click this field, then drag on the source image to measure it.")
        : tr("Manual entry is available. This snapshot has no physical scale for drawing.");
    m_length->setToolTip(measurement_tip);
    m_width->setToolTip(measurement_tip);
    m_height->setToolTip(measurement_tip);
    m_shadow->setToolTip(measurement_tip);
    m_snap->resetView();
    m_scale_sl->setValue(100);
    m_rot_sl->setValue(0);
    m_snap->setMarkerColor(effectiveColor());
    m_show_icon->setChecked(true);

    m_loading = false;
}

core::Contact ContactEditorDialog::readForm() const
{
    core::Contact c = m_before;   // preserve id, coords, artifact, timestamps, group

    c.label          = m_name->text().trimmed().toStdString();
    c.symbol         = m_symbol->currentData().toString().toStdString();
    c.classification = m_class->currentText().trimmed().toStdString();
    c.color_rgb      = m_color.isValid() ? m_color.rgba() : 0u;

    // Position is editable (moving the pick); same slot convention as loading.
    // Only adopt the spin value when it truly changed, so an untouched position
    // keeps its full stored precision (the spins display rounded values).
    {
        const double ceps = core::spatialRefIsProjected(m_before.spatial_ref) ? 5e-3 : 5e-7;
        if (std::fabs(m_coord_n->value() - m_before.lat) >= ceps) c.lat = m_coord_n->value();
        if (std::fabs(m_coord_e->value() - m_before.lon) >= ceps) c.lon = m_coord_e->value();
    }

    c.height_not_measurable = m_height_nm->isChecked();
    c.height_m       = static_cast<float>(m_height->value());
    c.shadow_m       = static_cast<float>(m_shadow->value());
    c.width_m        = static_cast<float>(m_width->value());
    c.length_m       = static_cast<float>(m_length->value());
    c.depth_m        = static_cast<float>(m_depth->value());
    c.burial_depth_m = static_cast<float>(m_burial->value());
    c.snapshot_across_m_per_px = m_measure_across_m_per_px;
    c.snapshot_along_m_per_px  = m_measure_along_m_per_px;
    c.pick_altitude_m          = m_measure_altitude_m;

    c.confidence     = static_cast<core::Confidence>(
                           std::clamp(m_confidence->currentIndex(), 0, 2));

    c.notes          = m_desc->toPlainText().toStdString();
    c.use_for_report = m_use_report->isChecked();

    c.tags.clear();
    for (int i = 0; i < m_tags_list->count(); ++i)
        c.tags.push_back(m_tags_list->item(i)->text().toStdString());
    return c;
}

void ContactEditorDialog::commitIfChanged()
{
    if (m_loading || m_index < 0) return;
    if (!findContact(m_before.id)) return;   // contact vanished — nothing to commit
    core::Contact after = readForm();
    if (editableEqual(after, m_before)) return;
    emit contactSaveRequested(m_before, after);
    m_before = after;                        // reflect the committed state
}

void ContactEditorDialog::updateNavState()
{
    const int n = static_cast<int>(m_ids.size());
    if (m_prev_btn) m_prev_btn->setEnabled(m_index > 0);
    if (m_next_btn) m_next_btn->setEnabled(m_index >= 0 && m_index < n - 1);
    if (m_title_lbl && n > 0 && m_index >= 0) {
        const core::Contact* c = findContact(m_ids[m_index]);
        const QString name = c ? QString::fromStdString(c->label) : QString();
        m_title_lbl->setText(tr("%1  (%2 of %3)")
            .arg(name).arg(m_index + 1).arg(n));
    }
}

void ContactEditorDialog::showContact(std::vector<uint64_t> ordered_ids,
                                      uint64_t current_id)
{
    commitIfChanged();
    m_ids = std::move(ordered_ids);
    int idx = 0;
    for (int i = 0; i < static_cast<int>(m_ids.size()); ++i)
        if (m_ids[i] == current_id) { idx = i; break; }
    m_index = -1;                 // force reload without re-committing
    loadIndex(idx);
}

void ContactEditorDialog::refresh(app::Project* project)
{
    if (m_loading) return;   // mid-load / mid-delete — the caller re-syncs after
    m_project = project;

    // Drop ids that no longer exist; keep the current one selected if possible.
    const uint64_t want = currentId();
    std::vector<uint64_t> kept;
    kept.reserve(m_ids.size());
    for (uint64_t id : m_ids)
        if (findContact(id)) kept.push_back(id);
    m_ids = std::move(kept);

    if (m_ids.empty()) { accept(); return; }

    int  idx   = 0;
    bool found = false;
    for (int i = 0; i < static_cast<int>(m_ids.size()); ++i)
        if (m_ids[i] == want) { idx = i; found = true; break; }
    if (!found)
        idx = std::clamp(m_index, 0, static_cast<int>(m_ids.size()) - 1);
    m_index = idx;

    // If the current contact survived and its stored state still matches the
    // snapshot we loaded, the change was elsewhere — keep the form (and any
    // in-progress edits) untouched; only the nav counts may have moved.
    if (found) {
        const core::Contact* cur = findContact(want);
        if (cur && editableEqual(*cur, m_before)) { updateNavState(); return; }
    }

    m_index = -1;          // force a reload without committing the stale form
    loadIndex(idx);
}

void ContactEditorDialog::done(int r)
{
    // QDialog::accept()/reject() bypass closeEvent, so this is the single
    // funnel for every close path — commit the pending edit before closing.
    commitIfChanged();
    QDialog::done(r);
}

} // namespace dolphin::ui
