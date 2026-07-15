// WaterfallWindow.Lifecycle.cpp — layer attach/detach, QC persistence, frequency band switch.

#include "ui/features/waterfall/WaterfallWindow.h"
#include "ui/features/waterfall/WaterfallQcStrip.h"
#include "ui/systems/AppState.h"
#include "ui/features/waterfall/WaterfallView.h"
#include "ui/features/waterfall/panels/WaterfallInspectorPanel.h"
#include "ui/features/waterfall/panels/WaterfallAnalysisPanel.h"
#include "app/layers/DataLayer.h"
#include "app/layers/LayerUtils.h"
#include "app/services/ImportService.h"
#include "app/tasks/OperationManager.h"
#include "core/Artifact.h"

#include <QComboBox>
#include <QLabel>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dolphin::ui {

namespace {

// Count the sidescan entries that belong to the selected frequency band.
// The bands come from per-entry frequency_hz, matching the display loader.
static int countSidescanForBand(const app::DataLayer* layer, float target_hz)
{
    if (!layer) return 0;
    if (target_hz <= 0.f) return layer->sidescanCount();

    const auto bands = app::sidescanFrequencyBands(layer->artifact_index);
    if (bands.size() < 2)
        return layer->sidescanCount();

    const float target_band = app::nearestFrequencyBand(bands, target_hz);
    int count = 0;
    for (const auto& e : layer->artifact_index.entries) {
        if (e.type != core::ArtifactType::Sidescan) continue;
        if (e.frequency_hz > 0.f && std::fabs(e.frequency_hz - target_band) < 1.f)
            ++count;
    }
    return count;
}

} // namespace

// -----------------------------------------------------------------------------
//  QC range persistence
// -----------------------------------------------------------------------------

QString WaterfallWindow::qcSettingsKey() const
{
    if (!m_layer) return {};
    return m_selected_frequency_hz > 0.f
        ? QString("qc/%1/band_%2")
              .arg(QString::fromStdString(m_layer->id))
              .arg(static_cast<int>(m_selected_frequency_hz))
        : QString("qc/%1").arg(QString::fromStdString(m_layer->id));
}

void WaterfallWindow::saveQcRanges()
{
    if (!m_layer || m_viewed_ranges.empty()) return;

    QSettings s;
    const QString key = qcSettingsKey();

    QStringList parts;
    for (const auto& [a, b] : m_viewed_ranges)
        parts << QString("%1-%2").arg(a).arg(b);
    s.setValue(key + "/viewedRanges",  parts.join(','));
    s.setValue(key + "/entriesPerRow", static_cast<double>(m_entries_per_row));

    m_layer->qc_viewed_fraction = m_qc_fraction;
}

void WaterfallWindow::loadQcRanges()
{
    m_viewed_ranges.clear();
    // Seed from the JSON-persisted value so cross-machine / fresh-install
    // sessions preserve the fraction even when QSettings has no range data.
    m_qc_fraction = m_layer ? m_layer->qc_viewed_fraction : 0.f;

    if (!m_layer) return;

    QSettings s;
    const QString key = qcSettingsKey();

    // Restore the previously measured entries-per-row ratio so estimatedTotalRows()
    // returns the right denominator instead of the freshly-reset 1.0 default.
    const float saved_epr = static_cast<float>(
        s.value(key + "/entriesPerRow", 1.0).toDouble());
    if (saved_epr > 1.01f && saved_epr <= 2.1f)
        m_entries_per_row = saved_epr;

    const QString saved = s.value(key + "/viewedRanges").toString();
    if (!saved.isEmpty()) {
        for (const QString& seg : saved.split(',')) {
            const QStringList p = seg.split('-');
            if (p.size() == 2) {
                bool ok1 = false, ok2 = false;
                const int a = p[0].toInt(&ok1);
                const int b = p[1].toInt(&ok2);
                if (ok1 && ok2 && a >= 0 && a < b)
                    m_viewed_ranges.push_back({a, b});
            }
        }
    }

    // Recompute fraction only when the entry count is known and QSettings had
    // range data.  Falls back to the JSON-seeded fraction when entries are
    // unknown (placeholder layer) or QSettings is empty (new machine / cleared).
    if (m_total_ssc_entries > 0 && !m_viewed_ranges.empty()) {
        const int total = estimatedTotalRows();
        if (total > 0) {
            int64_t viewed = 0;
            for (const auto& [a, b] : m_viewed_ranges)
                viewed += static_cast<int64_t>(b) - a;
            m_qc_fraction = std::clamp(
                static_cast<float>(viewed) / static_cast<float>(total), 0.f, 1.f);
            m_layer->qc_viewed_fraction = m_qc_fraction;
        }
    }

    if (m_qc_strip) {
        const int total = m_total_ssc_entries > 0 ? estimatedTotalRows() : 0;
        m_qc_strip->setData(total, m_viewed_ranges, 0, 0, m_qc_fraction);
    }
}

WaterfallWindow::~WaterfallWindow()
{
    saveQcRanges();
}

void WaterfallWindow::closeEvent(QCloseEvent* ev)
{
    saveQcRanges();
    QWidget::closeEvent(ev);
}

// -----------------------------------------------------------------------------
//  Data API
// -----------------------------------------------------------------------------

void WaterfallWindow::setLayer(app::DataLayer*     layer,
                               const std::string&  source_path,
                               uint64_t            source_size_bytes)
{
    saveQcRanges();   // persist ranges for the outgoing layer before replacing it

    m_scroll_debounce->stop();
    m_repipe_debounce->stop();
    if (m_op_mgr) m_op_mgr->cancelByKey("wf:pipeline");
    m_pending_abs_row   = -1;

    if (m_layer && m_analysis && m_inspector) {
        WaterfallParams draft = m_analysis->currentParams(
            m_inspector->currentPaletteIndex());
        draft.display_channel = m_display_channel;
        m_param_drafts[m_layer->id] = draft;
    }

    m_layer             = layer;
    m_source_path       = source_path;
    m_source_size_bytes = source_size_bytes;

    // Establish the incoming layer's complete processing state before starting
    // its asynchronous load. Controls retained from the outgoing line must never
    // become the new line's implicit TVG/AGC/destripe configuration.
    if (layer && m_analysis && m_inspector && m_view) {
        WaterfallParams applied = layer->sss_display_state.customized
            ? layer->sss_display_state.params
            : WaterfallParams{};
        const auto draft_it = m_param_drafts.find(layer->id);
        WaterfallParams controls = draft_it != m_param_drafts.end()
            ? draft_it->second : applied;
        // Destripe is deliberately opt-in for each viewer session. Older project
        // files may contain a true flag written by the former restore/apply
        // feedback loop; opening a line must never execute that legacy flag.
        applied.destripe.enabled = false;
        controls.destripe.enabled = false;
        // Global SSS palette — DisplayStateManager persists it as "sss/paletteIdx"
        // (one key for map, waterfall, right panel, and Views alike).
        const int palette = QSettings().value(QStringLiteral("sss/paletteIdx"),
                                              PaletteIndex::Greyscale).toInt();
        applied.palette = palette;
        controls.palette = palette;
        applied.slant_range_correction = layer->slant_range_corrected;
        m_display_channel = applied.display_channel;
        m_inspector->setPalette(palette);
        m_analysis->setParams(controls);          // draft only; never runs processing
        m_view->setParamsNoRebuild(applied);      // last explicitly applied state
    }

    const std::vector<float> bands = layer
        ? app::sidescanFrequencyBands(layer->artifact_index)
        : std::vector<float>{};
    const float preferred_hz = (layer && layer->frequency_hz > 0.f)
        ? layer->frequency_hz
        : 0.f;

    // A layer with low_frequency_hz == 0 and frequency_hz > 0 is a pinned
    // single-band layer (_HF or _LF from split import).  The waterfall always
    // filters to its band; the selector is hidden so the user cannot switch bands.
    const bool is_pinned_band = layer
        && layer->low_frequency_hz == 0.f
        && layer->frequency_hz > 0.f;

    m_selected_frequency_hz = (is_pinned_band || bands.size() < 2)
        ? preferred_hz
        : app::nearestFrequencyBand(bands, preferred_hz);

    if (m_freq_selector) {
        QSignalBlocker sb(m_freq_selector);
        m_freq_selector->clear();
        if (!is_pinned_band && bands.size() >= 2) {
            auto kHz = [](float hz) -> QString {
                return QString("%1 kHz").arg(static_cast<int>(hz / 1000.f + 0.5f));
            };
            for (size_t i = 0; i < bands.size(); ++i) {
                QString label;
                if (i == 0)
                    label = tr("HF ") + kHz(bands[i]);
                else if (i + 1 == bands.size())
                    label = tr("LF ") + kHz(bands[i]);
                else
                    label = tr("Band ") + kHz(bands[i]);
                m_freq_selector->addItem(label, QVariant(bands[i]));
            }
            const auto selected_it = std::find_if(bands.begin(), bands.end(),
                [&](float hz) { return std::fabs(hz - m_selected_frequency_hz) < 1.f; });
            if (selected_it != bands.end()) {
                m_freq_selector->setCurrentIndex(
                    static_cast<int>(std::distance(bands.begin(), selected_it)));
            }
            m_freq_selector->setVisible(true);
        } else {
            m_freq_selector->setVisible(false);
        }
    }

    m_total_ssc_entries = countSidescanForBand(m_layer, m_selected_frequency_hz);
    m_window_first_row  = 0;
    m_entries_per_row   = 1.0f;
    m_reset_view_next   = true;

    loadQcRanges();   // restore QC progress for the incoming layer

    // Clear window-local contact overlays so picks from the previous layer
    // don't bleed through onto the new waterfall image.
    resetContactTool();

    // Clear stale seabed picks from the previous layer; detection stays enabled
    // so it auto-runs when the new layer's pings arrive via setPings().
    m_view->resetSeabedForNewLayer();

    refreshInspector();
    if (m_inspector && layer)
        m_inspector->setActiveLine(layer->id);

    // Restore the global SSS palette. DisplayStateManager owns this setting and
    // persists it to "sss/paletteIdx"; do not fall back to per-layer/app defaults
    // here or opening the waterfall can clobber the currently active palette.
    if (layer && m_inspector) {
        const int pal = QSettings().value(QStringLiteral("sss/paletteIdx"),
                                          PaletteIndex::Greyscale).toInt();
        m_inspector->setPalette(pal);   // does NOT re-emit paletteChanged
    }

    // Start at the beginning of each new layer so Prev/Next navigation
    // always lands at the top of the waterfall, not some arbitrary midpoint.
    loadWindow(0);
}

void WaterfallWindow::resetContactTool()
{
    // Three surfaces must stay in sync: view tool state, panel pick button,
    // toolbar pick button.  setContactPickActive uses QSignalBlocker so it
    // doesn't re-emit contactToolChanged — we must update m_btn_contact directly.
    m_view->clearContacts();
    m_view->setContactTool(0);
    if (m_analysis) m_analysis->setContactPickActive(false);
    if (m_btn_contact) {
        QSignalBlocker sb(m_btn_contact);
        m_btn_contact->setChecked(false);
    }
    // Also drop any in-progress feature draw so a line change can't finish a feature
    // with vertices from the previous line.
    m_view->setFeatureTool(0);
    syncFeatureToolButtons(0);
}

void WaterfallWindow::clearLayer()
{
    saveQcRanges();   // persist before dropping the layer pointer

    m_scroll_debounce->stop();
    m_repipe_debounce->stop();
    m_pending_abs_row = -1;
    if (m_op_mgr) m_op_mgr->cancelByKey("wf:pipeline");
    setDataState(ViewerDataState::Idle);
    m_layer                   = nullptr;
    m_source_path.clear();
    m_selected_frequency_hz   = 0.f;
    m_total_ssc_entries       = 0;
    m_window_first_row        = 0;
    m_viewed_ranges.clear();
    m_qc_fraction             = 0.f;
    if (m_qc_strip) m_qc_strip->reset();
    if (m_freq_selector) {
        QSignalBlocker sb(m_freq_selector);
        m_freq_selector->clear();
        m_freq_selector->setVisible(false);
    }
    m_status_left->clear();
    finishProgress();
    m_view->clear();
    resetContactTool();
    m_vscroll->setRange(0, 0);
    refreshInspector();
}

void WaterfallWindow::refreshInspector()
{
    if (m_inspector)
        m_inspector->refresh(m_layer,
                             m_total_ssc_entries, m_entries_per_row,
                             m_view->samplesPerPing(),
                             m_view->lineLengthMetres(),
                             m_view->frequencyHz(),
                             m_layer ? m_layer->sonar_name : std::string{},
                             m_view->soundVelocityMs());
}

// -----------------------------------------------------------------------------
//  Frequency band switch
// -----------------------------------------------------------------------------

void WaterfallWindow::onFrequencyBandChanged(int index)
{
    if (!m_freq_selector || !m_layer) return;
    saveQcRanges();   // persist ranges for the outgoing band before changing state
    m_viewed_ranges.clear();
    m_qc_fraction = 0.f;
    if (m_qc_strip) m_qc_strip->reset();

    const QVariant data = m_freq_selector->itemData(index);
    m_selected_frequency_hz = data.isValid() ? data.toFloat() : 0.f;
    m_total_ssc_entries = countSidescanForBand(m_layer, m_selected_frequency_hz);
    m_window_first_row  = 0;
    m_entries_per_row   = 1.0f;
    m_reset_view_next   = true;
    loadQcRanges();    // restore QC progress for the incoming band
    loadWindow(0);
}

} // namespace dolphin::ui
