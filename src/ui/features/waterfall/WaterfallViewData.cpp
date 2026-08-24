// WaterfallViewData.cpp — data API, params API, query API, and row rebuild

#include "ui/features/waterfall/WaterfallView.h"
#include "ui/features/waterfall/processing/WaterfallContactDetector.h"
#include "ui/features/waterfall/processing/WaterfallPipelinePolicy.h"
#include "ui/features/waterfall/processing/WaterfallPipelineRunner.h"
#include "ui/features/waterfall/interaction/WaterfallToolPolicy.h"
#include "ui/features/waterfall/processing/SeabedAutoDetector.h"
#include "ui/features/waterfall/processing/WaterfallPingAssembler.h"
#include "ui/shared/processing/SssAmplitudeContext.h"
#include "ui/shared/processing/SssImagingAlgorithms.h"
#include "ui/mainwindow/AppSettingsDialog.h"
#include "geo/GeoUtils.h"
#include <QSettings>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace dolphin::ui {

void WaterfallView::updateCpuRendererParams()
{
    if (m_gl_initialized) {
        m_cpu_renderer_params_dirty = true;
        return;
    }
    m_renderer.setParams(m_params);
    m_cpu_renderer_params_dirty = false;
}

void WaterfallView::syncCpuRendererParams()
{
    if (!m_cpu_renderer_params_dirty) return;
    m_renderer.setParams(m_params);
    m_cpu_renderer_params_dirty = false;
}

// -----------------------------------------------------------------------------
//  Data API
// -----------------------------------------------------------------------------

void WaterfallView::setPings(const std::vector<core::SidescanPing>& pings,
                              bool preserve_view)
{
    setPreassembledRows(pings,
        runWaterfallPipeline(
            pings, m_params, m_seabed_auto_params, m_seabed_enabled,
            m_amplitude_context.get()),
        preserve_view);
}

void WaterfallView::setAmplitudeContext(
    std::shared_ptr<const imaging::SssAmplitudeContext> context)
{
    m_amplitude_context = std::move(context);
}

void WaterfallView::setPreassembledRows(std::vector<core::SidescanPing> raw_pings,
                                        WaterfallPipelineResult         result,
                                        bool                            preserve_view)
{
    // Invalidate any concurrent in-flight internal rebuild (from setParams) so
    // it cannot overwrite these rows when it eventually completes.
    m_pipeline_runner->cancel();

    if (!preserve_view)
        m_scroll.resetZoomPan();

    m_raw_pings = std::move(raw_pings);
    m_rows      = std::move(result.rows);

    applyManualSeabedPicks();

    // Honour the app-wide auto-stretch setting: apply percentile stretch when
    // enabled; fall back to full range [0,1] when the user has turned it off.
    const bool auto_stretch = QSettings().value(
        AppSettingsDialog::kKeyAutoStretch, true).toBool();
    if (auto_stretch) {
        m_stretch_low  = result.stretch_low;
        m_stretch_high = result.stretch_high;
    } else {
        m_stretch_low  = 0.f;
        m_stretch_high = 1.f;
    }
    m_params.display_low   = m_stretch_low;
    m_params.display_high  = m_stretch_high;
    updateCpuRendererParams();

    m_dirty             = true;
    m_render_generation.dataChanged();
    m_amp_profile_dirty = true;

    if (!preserve_view)
        m_scroll.scrollToEnd();
    update();
}

void WaterfallView::clear()
{
    m_pipeline_runner->cancel();
    m_rows.clear();
    m_raw_pings.clear();
    m_amplitude_context.reset();
    // Drop any in-progress feature draft so switching lines can't finish a feature
    // with stale vertices from the previous line.
    m_feature_tool = 0;
    m_feature_pts.clear();
    m_feature_px.clear();
    m_feature_pen_down = false;
    m_dirty              = true;
    m_render_generation.dataChanged();
    m_amp_profile_dirty  = true;
    update();
}

void WaterfallView::setSeabedChannel(int ch)
{
    m_seabed_channel = ch;
}

void WaterfallView::setSeabedTool(int tool)
{
    const auto selection = waterfalltools::selectSeabed(
        {waterfalltools::seabedToolFromIndex(m_seabed_tool),
         waterfalltools::contactToolFromIndex(m_contact_tool),
         waterfalltools::featureToolFromIndex(m_feature_tool)},
        waterfalltools::seabedToolFromIndex(tool));
    m_seabed_tool = static_cast<int>(selection.seabed);
    m_contact_tool = static_cast<int>(selection.contact);
    m_feature_tool = static_cast<int>(selection.feature);
    if (m_seabed_tool != 0) {
        m_feature_pts.clear();
        m_feature_px.clear();
        m_feature_pen_down = false;
    }
    if (m_seabed_tool != 1) {
        m_seabed.endDrag();
        m_pen_last_row = -1;
    }
    if (m_seabed_tool != 2) {
        m_box_anchor_row = -1;
        m_box_press_sx   = -1;
        m_box_press_sy   = -1;
    }
    update();
}

void WaterfallView::setContactTool(int tool)
{
    const auto selection = waterfalltools::selectContact(
        {waterfalltools::seabedToolFromIndex(m_seabed_tool),
         waterfalltools::contactToolFromIndex(m_contact_tool),
         waterfalltools::featureToolFromIndex(m_feature_tool)},
        waterfalltools::contactToolFromIndex(tool));
    m_seabed_tool = static_cast<int>(selection.seabed);
    m_contact_tool = static_cast<int>(selection.contact);
    m_feature_tool = static_cast<int>(selection.feature);
    if (m_contact_tool != 0) {
        m_seabed.endDrag();
        m_feature_pts.clear();
        m_feature_px.clear();
        m_feature_pen_down = false;
    }
    update();
}

void WaterfallView::setFeatureTool(int tool)
{
    const auto selection = waterfalltools::selectFeature(
        {waterfalltools::seabedToolFromIndex(m_seabed_tool),
         waterfalltools::contactToolFromIndex(m_contact_tool),
         waterfalltools::featureToolFromIndex(m_feature_tool)},
        waterfalltools::featureToolFromIndex(tool));
    m_seabed_tool = static_cast<int>(selection.seabed);
    m_contact_tool = static_cast<int>(selection.contact);
    m_feature_tool = static_cast<int>(selection.feature);
    // Switching tool (or off) discards any in-progress draft.
    m_feature_pts.clear();
    m_feature_px.clear();
    m_feature_pen_down = false;
    if (m_feature_tool != 0) {
        m_seabed.endDrag();
        setFocus(Qt::OtherFocusReason);   // receive Enter/Esc/Backspace
    }
    update();
}

void WaterfallView::clearContacts()
{
    m_contacts.clear();
    m_dirty = true;
    update();
}

int WaterfallView::detectContactCandidates(int sensitivity)
{
    auto candidates = WaterfallContactDetector::detect(
        m_rows, m_contacts, m_contact_class, sensitivity);
    for (const auto& contact : candidates) {
        m_contacts.push_back(contact);
        const auto& ping = m_rows[static_cast<size_t>(contact.row_idx)];
        double lat = 0.0, lon = 0.0;
        bool projected = false;
        rangeToGeo(contact.row_idx, contact.ch, contact.range_m,
                   lat, lon, projected);
        emit contactPicked(contact.row_idx, contact.ch, contact.range_m,
                           lat, lon, projected, QPixmap{}, 0.f, 0.f,
                           std::isfinite(ping.altitude_m) ? ping.altitude_m : 0.f);
    }
    const int detected = static_cast<int>(candidates.size());
    if (detected > 0) {
        m_dirty = true;
        update();
    }
    return detected;
}

void WaterfallView::refreshExternalContacts(const std::vector<core::Contact>& contacts,
                                             int window_first_row)
{
    m_external_contacts.clear();

    for (const auto& c : contacts) {
        WfContact wfc;
        wfc.id = c.id;   // lets marker double-click open this contact in the editor
        wfc.symbol = c.symbol;
        wfc.color_rgb = c.color_rgb;

        // A waterfall pick always has a slant range (the pick requires range_m > 0);
        // map/externally-placed contacts have range_m == 0. Discriminating on range_m
        // (not artifact_id > 0) means a pick on absolute row 0 keeps its waterfall
        // identity instead of falling into the nearest-nav map path.
        if (c.range_m > 0.f) {
            // Waterfall-picked: artifact_id stores the absolute ping row.
            const int local_row = static_cast<int>(c.artifact_id) - window_first_row;
            if (local_row < 0 || local_row >= static_cast<int>(m_rows.size())) continue;
            wfc.row_idx = local_row;
            wfc.ch      = (c.sample_idx == 0) ? core::SidescanChannel::Port
                                               : core::SidescanChannel::Starboard;
            wfc.range_m = c.range_m;
        } else {
            // Map-picked or externally placed: find the nearest loaded ping by lat/lon.
            if (c.lat == 0.0 && c.lon == 0.0) continue;
            int    best  = -1;
            double bdist = 1e18;
            for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
                if (m_rows[i].lat == 0.0 && m_rows[i].lon == 0.0) continue;
                const double dlat = m_rows[i].lat - c.lat;
                const double dlon = m_rows[i].lon - c.lon;
                const double d    = dlat*dlat + dlon*dlon;
                if (d < bdist) { bdist = d; best = i; }
            }
            // Only show if within ~0.005 deg (≈500 m) of a ping nav point.
            if (best < 0 || bdist > 2.5e-5) continue;
            wfc.row_idx = best;
            // Show map-placed contacts at nadir on both sides by adding two entries.
            wfc.ch      = core::SidescanChannel::Port;
            wfc.range_m = c.range_m > 0.f ? c.range_m : 0.f;
            m_external_contacts.push_back(wfc);
            wfc.ch = core::SidescanChannel::Starboard;
        }
        m_external_contacts.push_back(wfc);
    }

    m_dirty = true;
    update();
}

void WaterfallView::setShowSeabedLine(bool show)
{
    if (m_show_seabed_line == show) return;
    m_show_seabed_line = show;
    update();
}

void WaterfallView::setShowAmpBar(bool show)
{
    if (m_show_amp_bar == show) return;
    m_show_amp_bar = show;
    m_layout_dirty = true;
    m_dirty = true;
    update();
}

void WaterfallView::setOverlayParams(const WfOverlayParams& p)
{
    m_overlay_params = p;
    update();
}

void WaterfallView::setVerticalScale(float pings_per_cm)
{
    if (m_v_pings_per_cm == pings_per_cm) return;
    m_v_pings_per_cm = pings_per_cm;
    m_layout_dirty = true;
    m_dirty = true;
    update();
}

void WaterfallView::setHorizontalScale(float samples_per_cm)
{
    if (m_h_samples_per_cm == samples_per_cm) return;
    m_h_samples_per_cm = samples_per_cm;

    if (samples_per_cm <= 0.f) {
        m_scroll.setHZoom(0.f);
    } else {
        const float dpi_x = logicalDpiX() > 0 ? static_cast<float>(logicalDpiX()) : 96.f;
        m_scroll.setHZoom(dpi_x / 2.54f / samples_per_cm);
    }
    m_dirty = true;
    update();
}

void WaterfallView::clearSeabedDetection()
{
    m_seabed_enabled = false;
    m_manual_seabed.clear();
    for (auto& row : m_rows) {
        row.seabed = {};
        row.port_seabed = {};
        row.stbd_seabed = {};
        row.port_seabed_domain = core::SidescanRangeDomain::Slant;
        row.stbd_seabed_domain = core::SidescanRangeDomain::Slant;
        row.seabed_domain = core::SidescanRangeDomain::Slant;
    }
    m_dirty        = true;
    m_render_generation.geometryChanged();
    update();
}

void WaterfallView::resetSeabedForNewLayer()
{
    m_seabed_enabled = true;
    m_manual_seabed.clear();
    for (auto& row : m_rows) {
        row.seabed = {};
        row.port_seabed = {};
        row.stbd_seabed = {};
        row.port_seabed_domain = core::SidescanRangeDomain::Slant;
        row.stbd_seabed_domain = core::SidescanRangeDomain::Slant;
        row.seabed_domain = core::SidescanRangeDomain::Slant;
    }
    m_dirty        = true;
    m_render_generation.geometryChanged();
    update();
}

void WaterfallView::redetectSeabed(const SeabedAutoParams& params)
{
    m_seabed_enabled     = true;
    m_seabed_auto_params = params;
    // Imported/per-channel picks must not mask the newly requested detector
    // result. Manual edits are restored below from m_manual_seabed.
    for (auto& row : m_rows) {
        row.seabed = {};
        row.seabed_domain = core::SidescanRangeDomain::Slant;
        row.port_seabed = {};
        row.stbd_seabed = {};
        row.port_seabed_domain = core::SidescanRangeDomain::Slant;
        row.stbd_seabed_domain = core::SidescanRangeDomain::Slant;
    }

    if (!m_raw_pings.empty()) {
        // Detect on clean calibrated rows (pre-display) so beam/ARN/destripe/ML
        // output cannot bias amplitude structure seen by the detector.
        std::vector<core::SidescanPing> work = m_raw_pings;
        if (m_amplitude_context
                && m_amplitude_context->params_fingerprint
                    == imaging::sssAmplitudeParamsFingerprint(m_params)) {
            for (auto& ping : work)
                imaging::applyPerPingCalibration(ping, m_params);
        } else {
            imaging::applyCalibration(work, m_params);
        }
        std::vector<PingRow> clean = WaterfallPingAssembler::assemble(work, m_params);
        // Assembly faithfully imports source=2 bottom picks as manual. A user
        // explicitly requesting Redetect is stronger than that imported state;
        // current-session edits are the only manual picks restored afterwards.
        for (auto& row : clean) {
            row.seabed = {};
            row.seabed_domain = core::SidescanRangeDomain::Slant;
            row.port_seabed = {};
            row.stbd_seabed = {};
            row.port_seabed_domain = core::SidescanRangeDomain::Slant;
            row.stbd_seabed_domain = core::SidescanRangeDomain::Slant;
        }
        SeabedAutoDetector::detectAll(clean, params);
        if (params.smoothing > 0.f)
            SeabedAutoDetector::smooth(clean, static_cast<int>(params.smoothing));
        // Splice picks into display rows. Guard against size mismatch that can
        // occur if the assembler pairs differently on re-assembly (e.g., first
        // ping dropped due to nav filtering).
        const std::size_t splice_n = std::min(m_rows.size(), clean.size());
        for (std::size_t i = 0; i < splice_n; ++i) {
            m_rows[i].seabed = clean[i].seabed;
            m_rows[i].seabed_domain = clean[i].seabed_domain;
        }
        // Clear any tail rows that the clean assembly did not cover.
        for (std::size_t i = splice_n; i < m_rows.size(); ++i) {
            m_rows[i].seabed = {};
            m_rows[i].seabed_domain = core::SidescanRangeDomain::Slant;
        }
    } else {
        SeabedAutoDetector::detectAll(m_rows, params);
        if (params.smoothing > 0.f)
            SeabedAutoDetector::smooth(m_rows, static_cast<int>(params.smoothing));
    }
    // On raw slant-coordinate rows, an explicit redetection replaces an older
    // imported bottom reference for correction as well as for the overlay.
    // Ground-coordinate detector output is never a vertical altitude.
    for (auto& row : m_rows) {
        if (!row.seabed.detected
                || row.seabed_domain == core::SidescanRangeDomain::Ground
                || !(row.seabed.range_m > 0.f))
            continue;
        if (!row.port.empty()) row.port_altitude_m = row.seabed.range_m;
        if (!row.stbd.empty()) row.stbd_altitude_m = row.seabed.range_m;
    }
    applyManualSeabedPicks();
    m_dirty        = true;
    m_render_generation.geometryChanged();
    update();
}

// -----------------------------------------------------------------------------
//  Params API
// -----------------------------------------------------------------------------

void WaterfallView::setParams(const WaterfallParams& p)
{
    const bool needs_rebuild = waterfallpipeline::requiresRowRebuild(m_params, p);
    m_params = p;
    m_params.display_low  = m_stretch_low;    // restore data-derived stretch
    m_params.display_high = m_stretch_high;
    updateCpuRendererParams();

    const auto context = m_amplitude_context;
    const bool context_matches = waterfallpipeline::amplitudeContextMatches(
        context.get(), m_params);
    if (needs_rebuild && !m_raw_pings.empty() && context_matches) {
        auto raw_snap = std::make_shared<std::vector<core::SidescanPing>>(m_raw_pings);
        const auto   par    = m_params;
        const auto   seabed = m_seabed_auto_params;
        const bool   use_sb = m_seabed_enabled;
        m_pipeline_runner->start(
            raw_snap, par, seabed, use_sb, context,
            [this](WaterfallPipelineRunner::PingSnapshot snapshot,
                   WaterfallPipelineResult result) {
                setPreassembledRows(*snapshot, std::move(result), true);
            },
            [this] {
                m_dirty = true;
                update();
            });
    } else {
        m_dirty = true;
        update();
    }
}

void WaterfallView::setParamsNoRebuild(const WaterfallParams& p)
{
    m_params = p;
    m_params.display_low  = m_stretch_low;
    m_params.display_high = m_stretch_high;
    updateCpuRendererParams();
    m_dirty = true;
    update();
}

// -----------------------------------------------------------------------------
//  Query API
// -----------------------------------------------------------------------------

int WaterfallView::rowCount() const
{
    return static_cast<int>(m_rows.size());
}

WaterfallView::SrcStats WaterfallView::srcStats() const
{
    SrcStats s;
    s.total = static_cast<int>(m_rows.size());
    float amin = 1e18f, amax = 0.f, rmax = 0.f;
    for (const auto& r : m_rows) {
        const float seabed = (std::isfinite(r.seabed.range_m) && r.seabed.range_m > 0.f)
                               ? r.seabed.range_m : 0.f;
        const float alt    = seabed > 0.f ? seabed
                           : (std::isfinite(r.altitude_m) && r.altitude_m > 0.f)
                               ? r.altitude_m : 0.f;
        if (alt > 0.f) {
            ++s.with_alt;
            amin = std::min(amin, alt);
            amax = std::max(amax, alt);
        }
        if (std::isfinite(r.slant_range_m) && r.slant_range_m > rmax)
            rmax = r.slant_range_m;
    }
    s.alt_min_m   = (s.with_alt > 0) ? amin : 0.f;
    s.alt_max_m   = amax;
    s.range_max_m = rmax;
    return s;
}

const core::SidescanPing* WaterfallView::pingAt(int idx) const
{
    if (idx < 0 || idx >= static_cast<int>(m_raw_pings.size()))
        return nullptr;
    return &m_raw_pings[static_cast<size_t>(idx)];
}

int WaterfallView::samplesPerPing() const
{
    for (const auto& r : m_rows) {
        if (!r.port.empty()) return static_cast<int>(r.port.size());
        if (!r.stbd.empty()) return static_cast<int>(r.stbd.size());
    }
    return 0;
}

float WaterfallView::lineLengthMetres() const
{
    double total = 0.0;
    const int n = static_cast<int>(m_raw_pings.size());
    for (int i = 1; i < n; ++i) {
        const auto& a = m_raw_pings[i - 1].nav;
        const auto& b = m_raw_pings[i].nav;
        if (a.valid && b.valid && a.is_projected == b.is_projected)
            total += geo::navDistanceMetres(a, b);
    }
    return static_cast<float>(total);
}

float WaterfallView::frequencyHz() const
{
    for (const auto& ping : m_raw_pings)
        if (ping.frequency_hz > 0.f) return ping.frequency_hz;
    return 0.f;
}

float WaterfallView::soundVelocityMs() const
{
    for (const auto& ping : m_raw_pings)
        if (ping.sound_velocity_ms > 0.f) return ping.sound_velocity_ms;
    return 0.f;
}

void WaterfallView::scrollToRow(int row)
{
    if (m_scroll.scrollToRow(row, rowCount(), m_renderer.maxVisible())) {
        m_dirty = true;
        update();
    }
}

void WaterfallView::scrollToEnd()
{
    m_scroll.scrollToEnd();
    m_dirty = true;
    update();
}

// -----------------------------------------------------------------------------
//  Auto stretch — non-destructive percentile-based display normalisation.
//
//  Scans the assembled uint16 amplitude rows (already in physical space) and
//  finds the 1st / 99th percentile values.  These are stored in m_stretch_low/
//  high (normalised 0–1) so the viewer always uses the
//  full palette range regardless of the XTF's raw numeric range.
// -----------------------------------------------------------------------------

void WaterfallView::computeAutoStretch()
{
    // 1024-bin stack histogram: bin = v >> 6, ±0.1% precision vs 65536 bins.
    constexpr int kBins  = 1024;
    constexpr int kShift = 6;   // 65536 / 1024 = 64 = 2^6
    uint32_t hist[kBins] = {};
    for (const auto& row : m_rows) {
        for (uint16_t v : row.port) ++hist[v >> kShift];
        for (uint16_t v : row.stbd) ++hist[v >> kShift];
    }

    // Exclude bin 0 (covers v=0..63; captures water-column / nadir blanking).
    uint64_t total = 0;
    for (int i = 1; i < kBins; ++i) total += hist[i];

    if (total == 0) {
        m_stretch_low  = 0.f;
        m_stretch_high = 1.f;
    } else {
        const uint64_t lo_tgt = std::max(uint64_t(1), total / 100u);
        const uint64_t hi_tgt = total - lo_tgt;

        int      p01 = 1, p99 = kBins - 1;
        uint64_t cum      = 0;
        bool     found_lo = false;
        for (int i = 1; i < kBins; ++i) {
            cum += hist[i];
            if (!found_lo && cum >= lo_tgt) { p01 = i; found_lo = true; }
            if (cum >= hi_tgt)              { p99 = i; break; }
        }

        const int hi = p01 < kBins - 1 ? std::max(p99, p01 + 1) : kBins - 1;
        m_stretch_low  = float(p01 << kShift) / 65535.f;
        m_stretch_high = float(hi  << kShift) / 65535.f;
    }

    m_params.display_low  = m_stretch_low;
    m_params.display_high = m_stretch_high;
    updateCpuRendererParams();
}

} // namespace dolphin::ui
