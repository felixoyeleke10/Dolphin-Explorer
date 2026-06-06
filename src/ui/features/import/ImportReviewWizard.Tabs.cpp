// ImportReviewWizard.Tabs.cpp — Summary, Nav, CRS tab rebuilds + tab visibility + import button state.
#include "ui/features/import/ImportReviewWizard.h"
#include "ui/shared/dialogs/CrsPickerDialog.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace dolphin::ui {

QString ImportReviewWizard::modalityString(const io::ProbeResult& r) const
{
    QStringList parts;
    if (r.has_sidescan)     parts << tr("Sidescan");
    if (r.has_subbottom)    parts << tr("Sub-Bottom");
    if (r.has_magnetometer) parts << tr("Magnetometer");
    if (r.has_multibeam)    parts << tr("Multibeam");
    return parts.isEmpty() ? tr("Unknown") : parts.join(" / ");
}

// -----------------------------------------------------------------------------
//  Summary tab
// -----------------------------------------------------------------------------

void ImportReviewWizard::rebuildSummaryTab()
{
    clearLayout(m_summary_layout);

    if (m_entries.isEmpty()) {
        auto* lbl = new QLabel(tr("No files added yet."), m_summary_content);
        lbl->setObjectName("dlgLabelMeta");
        m_summary_layout->addWidget(lbl);
        m_summary_layout->addStretch();
        return;
    }

    for (const auto& e : m_entries) {
        auto* row = new QFrame(m_summary_content);
        auto* hl  = new QHBoxLayout(row);
        hl->setContentsMargins(Theme::kSpacing1, 2, Theme::kSpacing1, 2);
        hl->setSpacing(Theme::kSpacing3);

        auto* name = new QLabel("<b>" + e.file_name + "</b>", row);
        hl->addWidget(name);

        if (e.probing) {
            hl->addWidget(new QLabel(tr("Scanning..."), row), 1);
        } else if (!e.result.success) {
            auto* err = new QLabel(tr("Error: ") + QString::fromStdString(e.result.error_message), row);
            err->setObjectName("dlgLabelDanger");
            hl->addWidget(err, 1);
        } else {
            const io::ProbeResult& r = e.result;

            QString info = QString::fromStdString(r.format_name)
                         + "  ·  " + modalityString(r) + "  ·  ";
            if (!r.coord_valid) {
                info += tr("No coordinates");
            } else if (r.is_projected) {
                info += tr("Projected  X %1–%2  Y %3–%4")
                    .arg(r.coord_min_x, 0, 'f', 0)
                    .arg(r.coord_max_x, 0, 'f', 0)
                    .arg(r.coord_min_y, 0, 'f', 0)
                    .arg(r.coord_max_y, 0, 'f', 0);
            } else {
                info += tr("Geographic  %1°–%2°  %3°–%4°")
                    .arg(r.coord_min_x, 0, 'f', 3)
                    .arg(r.coord_max_x, 0, 'f', 3)
                    .arg(r.coord_min_y, 0, 'f', 3)
                    .arg(r.coord_max_y, 0, 'f', 3);
            }
            if (r.estimated_record_count > 0)
                info += tr("  ·  ~%1 records")
                    .arg(QLocale().toString(r.estimated_record_count));

            auto* info_lbl = new QLabel(info, row);
            info_lbl->setObjectName("dlgLabelMeta");
            hl->addWidget(info_lbl, 1);

            // Show warnings that have no dedicated tab. CRS-related diagnostics
            // (units_contradicted) are owned by the CRS tab and shown there.
            if (!r.units_contradicted) {
                for (const auto& w : r.warnings) {
                    auto* wlbl = new QLabel(
                        QString::fromStdString(w), m_summary_content);
                    wlbl->setObjectName("dlgLabelCaution");
                    m_summary_layout->addWidget(row);
                    m_summary_layout->addWidget(wlbl);
                    row = nullptr;
                    break;
                }
            }
        }

        if (row) m_summary_layout->addWidget(row);
    }

    m_summary_layout->addStretch();
}

// -----------------------------------------------------------------------------
//  Nav tab
// -----------------------------------------------------------------------------

void ImportReviewWizard::rebuildNavTab()
{
    clearLayout(m_nav_layout);

    bool any_probing = false;
    bool any_file    = false;

    for (const auto& e : m_entries) {
        if (e.probing) { any_probing = true; continue; }
        if (!e.done) continue;

        any_file = true;
        const io::ProbeResult& r = e.result;

        auto* sect = new QFrame(m_nav_content);
        sect->setObjectName("dlgSection");
        auto* vl = new QVBoxLayout(sect);
        vl->setContentsMargins(10, Theme::kSpacing2, 10, Theme::kSpacing2);
        vl->setSpacing(3);

        vl->addWidget(new QLabel("<b>" + e.file_name + "</b>", sect));

        if (!r.success) {
            auto* lbl = new QLabel(
                tr("Error: ") + QString::fromStdString(r.error_message), sect);
            lbl->setObjectName("dlgLabelDanger");
            vl->addWidget(lbl);
            m_nav_layout->addWidget(sect);
            continue;
        }

        if (!r.coord_valid) {
            auto* lbl = new QLabel(tr("No navigation data found"), sect);
            lbl->setObjectName("dlgLabelMeta");
            vl->addWidget(lbl);
            m_nav_layout->addWidget(sect);
            continue;
        }

        // Coordinate type + bounding box
        const int prec = r.is_projected ? 1 : 5;
        const QString type = r.is_projected ? tr("Projected (m)") : tr("Geographic (deg)");
        const QString range = r.is_projected
            ? tr("X: %1 to %2    Y: %3 to %4")
                  .arg(r.coord_min_x, 0, 'f', prec).arg(r.coord_max_x, 0, 'f', prec)
                  .arg(r.coord_min_y, 0, 'f', prec).arg(r.coord_max_y, 0, 'f', prec)
            : tr("Lon: %1 to %2    Lat: %3 to %4")
                  .arg(r.coord_min_x, 0, 'f', prec).arg(r.coord_max_x, 0, 'f', prec)
                  .arg(r.coord_min_y, 0, 'f', prec).arg(r.coord_max_y, 0, 'f', prec);

        auto* range_lbl = new QLabel(type + "    " + range, sect);
        range_lbl->setObjectName("dlgLabelMono");
        vl->addWidget(range_lbl);

        // Sample coordinates
        if (!r.coord_samples.empty()) {
            const int n = std::min((int)r.coord_samples.size(), 5);
            QString s = tr("Samples: ");
            for (int j = 0; j < n; ++j) {
                if (j) s += "   ";
                s += QString("(%1, %2)")
                    .arg(r.coord_samples[j].x, 0, 'f', prec)
                    .arg(r.coord_samples[j].y, 0, 'f', prec);
            }
            auto* samp = new QLabel(s, sect);
            samp->setObjectName("dlgLabelMono");
            vl->addWidget(samp);
        }

        // Quality warnings
        if (r.possibly_swapped) {
            auto* w = new QLabel(tr("[!] X/Y coordinates may be transposed"), sect);
            w->setObjectName("dlgLabelCaution");
            vl->addWidget(w);
        }
        if (r.units_contradicted) {
            auto* w = new QLabel(
                tr("[!] Declared geographic CRS but values appear projected"), sect);
            w->setObjectName("dlgLabelCaution");
            vl->addWidget(w);
        }

        m_nav_layout->addWidget(sect);
    }

    if (any_probing) {
        auto* lbl = new QLabel(tr("Scanning files..."), m_nav_content);
        lbl->setObjectName("dlgLabelMeta");
        m_nav_layout->addWidget(lbl);
    } else if (!any_file) {
        auto* lbl = new QLabel(tr("No files added yet."), m_nav_content);
        lbl->setObjectName("dlgLabelMeta");
        m_nav_layout->addWidget(lbl);
    }

    m_nav_layout->addStretch();
}

// -----------------------------------------------------------------------------
//  CRS tab
// -----------------------------------------------------------------------------

void ImportReviewWizard::rebuildCrsTab()
{
    clearLayout(m_crs_layout);
    m_crs_value_label = nullptr;  // owned by the old layout, now deleted

    bool any_probing = false;
    int  n_review    = 0;

    for (const auto& e : m_entries) {
        if (e.probing) { any_probing = true; continue; }
        if (e.done && e.result.success && e.result.needs_crs_review)
            ++n_review;
    }

    if (any_probing) {
        auto* lbl = new QLabel(tr("Scanning files..."), m_crs_content);
        lbl->setObjectName("dlgLabelMeta");
        m_crs_layout->addWidget(lbl);
        m_crs_layout->addStretch();
        return;
    }

    if (m_entries.isEmpty()) {
        auto* lbl = new QLabel(tr("No files added yet."), m_crs_content);
        lbl->setObjectName("dlgLabelMeta");
        m_crs_layout->addWidget(lbl);
        m_crs_layout->addStretch();
        return;
    }

    if (n_review == 0) {
        m_crs_layout->addWidget(
            new QLabel(tr("All files have unambiguous CRS — no review needed."),
                       m_crs_content));
        m_crs_layout->addStretch();
        return;
    }

    // -- Single project-level CRS picker --------------------------------------
    auto* sect = new QFrame(m_crs_content);
    sect->setObjectName("dlgSection");
    auto* vl = new QVBoxLayout(sect);
    vl->setContentsMargins(10, Theme::kSpacing3, 10, Theme::kSpacing3);
    vl->setSpacing(Theme::kSpacing2);

    auto* desc = new QLabel(
        tr("%n file(s) contain projected coordinates. "
           "Confirm the source CRS so positions are correctly georeferenced.",
           "", n_review),
        sect);
    desc->setObjectName("dlgLabelMeta");
    desc->setWordWrap(true);
    vl->addWidget(desc);

    // Warn when more than one file needs CRS review — a single CRS is applied
    // to all of them, so files from different UTM zones must be imported separately.
    if (n_review > 1) {
        auto* warn = new QLabel(
            tr("<b>Note:</b> one CRS will be applied to all %n files listed below. "
               "If your files span different UTM zones, import each group separately.",
               "", n_review),
            sect);
        warn->setObjectName("dlgLabelWarning");
        warn->setWordWrap(true);
        vl->addWidget(warn);
    }

    auto* crs_row = new QWidget(sect);
    auto* hl      = new QHBoxLayout(crs_row);
    hl->setContentsMargins(0, 2, 0, 0);
    hl->setSpacing(Theme::kSpacing3);
    hl->addWidget(new QLabel(tr("CRS:"), crs_row));

    m_crs_value_label = new QLabel(crs_row);
    m_crs_value_label->setObjectName("dlgLabelMeta");
    if (m_project_crs.empty())
        m_crs_value_label->setText(tr("<i>Not set</i>"));
    else
        m_crs_value_label->setText(QString::fromStdString(m_project_crs.id));

    auto* pick_btn = new QPushButton(tr("Pick CRS..."), crs_row);
    pick_btn->setObjectName("dlgBtnSecondary");
    pick_btn->setFixedHeight(Theme::kColorBtnH);

    connect(pick_btn, &QPushButton::clicked, this, [this]() {
        CrsPickerDialog dlg(m_project_crs, this);
        if (dlg.exec() == QDialog::Accepted) {
            m_project_crs = dlg.selectedRef();
            if (m_crs_value_label)
                m_crs_value_label->setText(
                    QString::fromStdString(m_project_crs.id));
            for (int i = 0; i < m_entries.size(); ++i)
                updateFileRow(i);
            updateImportButton();
        }
    });

    hl->addWidget(m_crs_value_label, 1);
    hl->addWidget(pick_btn);
    vl->addWidget(crs_row);
    m_crs_layout->addWidget(sect);

    // -- Per-file diagnostic rows (info only) ---------------------------------
    for (const auto& e : m_entries) {
        if (!e.done || !e.result.success || !e.result.needs_crs_review) continue;
        const io::ProbeResult& r = e.result;

        auto* fsect = new QFrame(m_crs_content);
        fsect->setObjectName("dlgSection");
        auto* fvl = new QVBoxLayout(fsect);
        fvl->setContentsMargins(10, Theme::kSpacing1, 10, Theme::kSpacing1);
        fvl->setSpacing(2);

        fvl->addWidget(new QLabel("<b>" + e.file_name + "</b>", fsect));

        // Detection hint
        QString det;
        if (!r.coord_valid)       det = tr("No coordinate data");
        else if (r.is_projected)  det = tr("Detected: Projected (metre-scale values)");
        else                      det = tr("Detected: Geographic");
        if (r.possibly_swapped)   det += tr("  [!] X/Y may be swapped");
        if (r.units_contradicted) det += tr("  [!] Declared geo, projected values");
        auto* det_lbl = new QLabel(det, fsect);
        det_lbl->setObjectName("dlgLabelMeta");
        fvl->addWidget(det_lbl);

        if (!r.coord_samples.empty()) {
            const int prec = r.is_projected ? 1 : 6;
            QString s = tr("Samples: ");
            for (int j = 0, n = std::min((int)r.coord_samples.size(), 3); j < n; ++j) {
                if (j) s += "  ";
                s += QString("(%1, %2)")
                         .arg(r.coord_samples[j].x, 0, 'f', prec)
                         .arg(r.coord_samples[j].y, 0, 'f', prec);
            }
            auto* samp = new QLabel(s, fsect);
            samp->setObjectName("dlgLabelMono");
            fvl->addWidget(samp);
        }

        m_crs_layout->addWidget(fsect);
    }

    m_crs_layout->addStretch();
}

// -----------------------------------------------------------------------------
//  Tab visibility + import button state
// -----------------------------------------------------------------------------

void ImportReviewWizard::updateTabVisibility()
{
    // Aggregate flags from all probed entries.
    bool any_probing  = false;
    bool has_nav      = false;
    bool has_heading  = false;
    bool has_channels = false;
    bool has_header   = false;

    for (const auto& e : m_entries) {
        if (e.probing) { any_probing = true; continue; }
        if (!e.done || !e.result.success) continue;
        if (e.result.coord_valid)                  has_nav      = true;
        if (e.result.heading_valid)                has_heading  = true;
        if (!e.result.channels.empty())            has_channels = true;
        if (!e.result.text_header_excerpt.empty()) has_header   = true;
    }

    // -- Per-kind visibility rules -----------------------------------------
    // Summary  — always: every import needs an overview.
    // Nav      — while probing (show "Scanning...") or when any file has coords.
    //            Hidden after all probes finish with no positional data at all.
    // CRS      — always: gates the Import button regardless of sensor type.
    // Header   — only for formats that carry a text header (currently SEG-Y).
    //            Appears dynamically once the first such file is probed.
    //
    // To add a new sensor-specific tab (e.g. TabKind::Channels for SSS):
    //   1. Add the kind to the TabKind enum in the header.
    //   2. Call makeTab(TabKind::Channels, ...) in buildTabSection().
    //   3. Add a case here with the appropriate predicate.
    //   4. Add rebuildChannelsTab() and call it from onProbeFinished().
    for (const auto& spec : m_tab_specs) {
        bool visible = true;
        switch (spec.kind) {
        case TabKind::Summary:  visible = true;                       break;
        case TabKind::Nav:      visible = any_probing || has_nav;     break;
        case TabKind::Crs:      visible = true;                       break;
        case TabKind::Heading:  visible = any_probing || has_heading; break;
        case TabKind::Channels: visible = has_channels;               break;
        case TabKind::Header:   visible = has_header;                 break;
        }
        m_tabs->setTabVisible(spec.idx, visible);
    }
}

void ImportReviewWizard::updateImportButton()
{
    if (m_entries.isEmpty()) {
        m_import_btn->setEnabled(false);
        m_status_label->setText(tr("Add files to begin."));
        return;
    }

    bool any_probing = false;
    int  n_valid     = 0;
    int  n_needs_crs = 0;

    for (const auto& e : m_entries) {
        if (e.probing) { any_probing = true; continue; }
        if (e.done && e.result.success) {
            ++n_valid;
            // A file blocks import when CRS review is needed but no confirmed CRS exists.
            // "Confirmed" = project-level CRS was set by the user, OR the file has an
            // exact declared CRS (e.g. EPSG:4326). An inexact CRS (PROJECTED:UNKNOWN) is
            // not sufficient on its own.
            if (e.result.needs_crs_review) {
                // When the header CRS contradicts the coordinate magnitudes, the declared
                // CRS is the wrong (geographic) one — only a user-confirmed project CRS counts.
                const bool contradicted = e.result.units_contradicted;
                const bool confirmed = !m_project_crs.empty()
                    || (!contradicted && !e.result.declared_crs.empty() && e.result.declared_crs.exact);
                if (!confirmed) ++n_needs_crs;
            }
        }
    }

    if (any_probing) {
        m_import_btn->setEnabled(false);
        m_status_label->setText(tr("Scanning files..."));
        return;
    }

    m_import_btn->setEnabled(n_valid > 0 && n_needs_crs == 0);

    if (n_needs_crs > 0)
        m_status_label->setText(
            tr("%n file(s) need CRS confirmation before import — see CRS tab.", "", n_needs_crs));
    else if (n_valid > 0)
        m_status_label->setText(
            tr("%n file(s) ready to import.", "", n_valid));
    else
        m_status_label->setText(tr("No valid files."));
}

} // namespace dolphin::ui
