// ImportReviewWizard.Tabs.cpp — shared tab infrastructure: modalityString,
// updateTabVisibility, updateImportButton.
// Sensor-specific tab content lives in ImportReviewWizard.{SSS,SBP,Channels,Nav,CRS,Summary}.cpp
#include "ui/features/import/ImportReviewWizard.h"
#include "ui/shared/UiUtils.h"

#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QTabWidget>

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
//  Tab visibility
// -----------------------------------------------------------------------------

void ImportReviewWizard::updateTabVisibility()
{
    bool any_probing  = false;
    bool has_nav      = false;
    bool has_heading  = false;
    bool has_channels = false;
    bool has_header   = false;

    for (const auto& e : m_entries) {
        if (e.probing) { any_probing = true; continue; }
        if (!e.done || !e.result.success || e.modality_mismatch) continue;
        if (e.result.coord_valid)                  has_nav      = true;
        if (e.result.heading_valid)                has_heading  = true;
        if (!e.result.channels.empty())            has_channels = true;
        if (!e.result.text_header_excerpt.empty()) has_header   = true;
    }

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

// -----------------------------------------------------------------------------
//  Import button state
// -----------------------------------------------------------------------------

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
    int  n_mismatch  = 0;

    for (const auto& e : m_entries) {
        if (e.probing) { any_probing = true; continue; }
        if (e.done && e.result.success) {
            if (e.modality_mismatch) { ++n_mismatch; continue; }
            ++n_valid;
            if (e.result.needs_crs_review) {
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

    if (n_mismatch > 0) {
        m_import_btn->setEnabled(false);
        m_status_label->setText(
            tr("%n file(s) don't match the selected sensor type and cannot be imported.",
               "", n_mismatch));
        return;
    }

    // Fallback mixed-batch guard for drag-drop (no sensor filter active).
    if (m_module_filter.empty()) {
        bool has_sss = false;
        bool has_sbp = false;
        for (const auto& e : m_entries) {
            if (!e.done || !e.result.success) continue;
            if (e.result.has_sidescan  && !e.result.has_subbottom) has_sss = true;
            if (e.result.has_subbottom && !e.result.has_sidescan)  has_sbp = true;
        }
        if (has_sss && has_sbp) {
            for (auto& e : m_entries) {
                if (!e.done || !e.result.success || !e.status_label) continue;
                const bool is_sss = e.result.has_sidescan  && !e.result.has_subbottom;
                const bool is_sbp = e.result.has_subbottom && !e.result.has_sidescan;
                if (is_sss || is_sbp) {
                    e.status_label->setText(tr("Mixed batch"));
                    e.status_label->setProperty("state", QLatin1String("error"));
                    e.status_label->style()->unpolish(e.status_label);
                    e.status_label->style()->polish(e.status_label);
                }
            }
            m_import_btn->setEnabled(false);
            m_status_label->setText(
                tr("Sidescan and Sub-Bottom files cannot be imported together — "
                   "import each sensor type in a separate batch."));
            return;
        }
    }

    m_import_btn->setEnabled(n_valid > 0 && n_needs_crs == 0);

    if (n_needs_crs > 0)
        m_status_label->setText(
            tr("%n file(s) need CRS confirmation before import — see CRS tab.", "", n_needs_crs));
    else if (n_valid > 0)
        m_status_label->setText(tr("%n file(s) ready to import.", "", n_valid));
    else
        m_status_label->setText(tr("No valid files."));
}

} // namespace dolphin::ui
