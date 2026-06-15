// WaterfallWindow.Params.cpp — display params, palette, nav processing, settings application.

#include "ui/features/waterfall/WaterfallWindow.h"
#include "app/layers/DataLayer.h"
#include "ui/features/waterfall/WaterfallView.h"
#include "ui/features/waterfall/panels/WaterfallInspectorPanel.h"
#include "ui/features/waterfall/panels/WaterfallAnalysisPanel.h"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>

namespace dolphin::ui {

void WaterfallWindow::pushParams()
{
    if (!m_view || !m_analysis || !m_inspector) return;
    WaterfallParams p = m_analysis->currentParams(m_inspector->currentPaletteIndex());
    p.display_channel = m_display_channel;
    m_view->setParams(p);
}

void WaterfallWindow::setPalette(int idx)
{
    if (!m_inspector) return;
    if (m_inspector->currentPaletteIndex() == idx) return;  // already set — skip pushParams
    m_inspector->setPalette(idx);   // updates combo without re-emitting
    pushParams();
}

void WaterfallWindow::setDisplayChannel(DisplayChannel ch)
{
    if (m_display_channel == ch) return;
    m_display_channel = ch;
    if (m_view) pushParams();
}

void WaterfallWindow::scheduleNavProcessing(const NavProcessingParams& nav)
{
    if (!m_view || m_view->rawPings().empty()) return;

    m_load_cancel.cancel();
    m_load_cancel.reset();
    auto cancel = m_load_cancel;
    const int gen = ++m_load_gen;
    setDataState(ViewerDataState::Processing);
    startProgress();

    const WaterfallParams  params  = m_view->params();
    const SeabedAutoParams seabed  = m_analysis ? m_analysis->currentSeabedAutoParams()
                                                 : m_view->seabedAutoParams();
    const bool seabed_en           = m_view->seabedEnabled();
    auto raw = m_view->rawPings();

    struct Repipe {
        std::vector<core::SidescanPing> raw_pings;
        WaterfallView::WfPipelineResult pipeline;
    };
    auto* watcher = new QFutureWatcher<Repipe>(this);
    connect(watcher, &QFutureWatcher<Repipe>::finished,
            this, [this, watcher, gen]() {
                watcher->deleteLater();
                if (gen != m_load_gen) return;
                finishProgress();
                try {
                    auto r = watcher->result();
                    m_view->setPreassembledRows(std::move(r.raw_pings),
                                               std::move(r.pipeline),
                                               /*preserve_view=*/true);
                    pushParams();
                    setDataState(ViewerDataState::Ready);
                } catch (...) {
                    setDataState(ViewerDataState::Failed);
                }
            });
    watcher->setFuture(QtConcurrent::run(
        [r = std::move(raw), nav, params, seabed, seabed_en, cancel]() mutable -> Repipe {
            if (cancel.isCancelled()) return {};
            r = WaterfallView::runNavCorrections(std::move(r), nav);
            if (cancel.isCancelled()) return {};
            Repipe out;
            out.pipeline  = WaterfallView::runPipeline(r, params, seabed, seabed_en);
            out.raw_pings = std::move(r);
            return out;
        }));
}

void WaterfallWindow::applyNavToLine(const NavProcessingParams& p)
{
    scheduleNavProcessing(p);
}

void WaterfallWindow::applyNavToAll(const NavProcessingParams& p)
{
    scheduleNavProcessing(p);
    emit navProcessAllLinesRequested(p);
}

const WaterfallParams& WaterfallWindow::currentParams() const
{
    return m_view->params();
}

const std::string& WaterfallWindow::currentLayerId() const
{
    static const std::string kEmpty;
    return m_layer ? m_layer->id : kEmpty;
}

WaterfallSettingsDialog::Settings WaterfallWindow::wfSettings() const
{
    WaterfallSettingsDialog::Settings s = WaterfallSettingsDialog::loadDefaults();
    s.window_size     = m_window_size;
    s.display_channel = m_display_channel;
    s.show_amp_bar    = m_show_amp_bar;
    if (m_view)
        s.overlay = m_view->overlayParams();
    return s;
}

void WaterfallWindow::applyWfSettings(const WaterfallSettingsDialog::Settings& s)
{
    m_window_size     = s.window_size;
    m_display_channel = s.display_channel;
    m_show_amp_bar    = s.show_amp_bar;
    if (m_view) {
        m_view->setShowAmpBar(s.show_amp_bar);
        m_view->setOverlayParams(s.overlay);
        pushParams();  // applies display_channel to the live view immediately
    }
    if (m_inspector)
        m_inspector->setAmpBarChecked(s.show_amp_bar);
}

void WaterfallWindow::applyExternalParams(const WaterfallParams& p)
{
    if (!m_view) return;
    if (m_analysis) m_analysis->setParams(p);

    // Params that affect the processing pipeline (TVG/ARC/AGC/ARN/destripe/
    // beam-pattern/ML) must run off the UI thread.  Display-only changes
    // (palette, gain, contrast, SRC flag, display channel) are fast-path.
    const WaterfallParams& cur = m_view->params();
    const bool needs_repipe = !m_view->rawPings().empty()
        && ((p.agc          != cur.agc)
         || (p.tvg          != cur.tvg)
         || (p.arn          != cur.arn)
         || (p.destripe     != cur.destripe)
         || (p.beam_pattern != cur.beam_pattern)
         || (p.arc          != cur.arc)
         || (p.ml_enhance   != cur.ml_enhance));

    if (needs_repipe) {
        m_view->setParamsNoRebuild(p);   // commit new params; skip sync rebuild
        invalidateProcessedCache();      // schedule async re-pipeline
    } else {
        m_view->setParams(p);
    }
    flashProgress();
    emit paramsApplied();
}

void WaterfallWindow::applyExternalParamsToAll(const WaterfallParams& p)
{
    applyExternalParams(p);
    emit applyToAllRequested();
}

} // namespace dolphin::ui
