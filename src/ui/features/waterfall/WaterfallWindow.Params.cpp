// WaterfallWindow.Params.cpp — display params, palette, nav processing, settings application.

#include "ui/features/waterfall/WaterfallWindow.h"
#include "app/layers/DataLayer.h"
#include "app/tasks/OperationManager.h"
#include "ui/features/waterfall/WaterfallView.h"
#include "ui/features/waterfall/panels/WaterfallInspectorPanel.h"
#include "ui/features/waterfall/panels/WaterfallAnalysisPanel.h"

#include <QFutureWatcher>
#include <QSettings>
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
    if (m_view) {
        WaterfallParams p = m_view->params();
        p.palette = idx;
        m_view->setParams(p);
    }
}

void WaterfallWindow::setDisplayChannel(DisplayChannel ch)
{
    if (m_display_channel == ch) return;
    m_display_channel = ch;
    if (m_view) {
        WaterfallParams p = m_view->params();
        p.display_channel = ch;
        m_view->setParams(p);
    }
}

void WaterfallWindow::scheduleNavProcessing(const NavProcessingParams& nav)
{
    if (!m_view || m_view->rawPings().empty()) return;

    if (!m_op_mgr) return;
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
        bool ok = false;
    };
    // Keyed "wf:pipeline" so this supersedes any in-flight load/repipe/nav run.
    m_op_mgr->run<Repipe>(
        tr("Waterfall nav processing"),
        [r = std::move(raw), nav, params, seabed, seabed_en]
        (app::CancellationToken cancel) mutable -> Repipe {
            Repipe out;
            try {
                if (cancel.isCancelled()) return out;
                r = WaterfallView::runNavCorrections(std::move(r), nav);
                if (cancel.isCancelled()) return out;
                out.pipeline  = WaterfallView::runPipeline(r, params, seabed, seabed_en);
                out.raw_pings = std::move(r);
                out.ok = true;
            } catch (...) { out.ok = false; }
            return out;
        },
        [this](Repipe r) {
            finishProgress();
            if (!r.ok) { setDataState(ViewerDataState::Failed); return; }
            m_view->setPreassembledRows(std::move(r.raw_pings),
                                        std::move(r.pipeline),
                                        /*preserve_view=*/true);
            setDataState(ViewerDataState::Ready);
        },
        "wf:pipeline",
        /*heavy=*/false);
}

void WaterfallWindow::applyNavToLine(const NavProcessingParams& p)
{
    scheduleNavProcessing(p);
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
        WaterfallParams p = m_view->params();
        p.display_channel = s.display_channel;
        m_view->setParams(p);
    }
}

void WaterfallWindow::applyExternalParams(const WaterfallParams& p)
{
    if (!m_view) return;

    WaterfallParams applied = p;
    // Waterfall palette is global. Stored per-layer WaterfallParams may carry an
    // older palette value, so normalize it before touching UI/render state.
    applied.palette = QSettings().value(QStringLiteral("waterfall/paletteIdx"),
                                        PaletteIndex::Greyscale).toInt();
    if (m_inspector)
        m_inspector->setPalette(applied.palette);
    if (m_analysis) m_analysis->setParams(applied);

    // Params that affect the processing pipeline (TVG/ARC/AGC/ARN/destripe/
    // beam-pattern/ML) must run off the UI thread.  Display-only changes
    // (palette, gain, contrast, SRC flag, display channel) are fast-path.
    const WaterfallParams& cur = m_view->params();
    const bool needs_repipe = !m_view->rawPings().empty()
        && ((applied.agc          != cur.agc)
         || (applied.tvg          != cur.tvg)
         || (applied.arn          != cur.arn)
         || (applied.destripe     != cur.destripe)
         || (applied.beam_pattern != cur.beam_pattern)
         || (applied.arc          != cur.arc)
         || (applied.ml_enhance   != cur.ml_enhance));

    if (needs_repipe) {
        m_view->setParamsNoRebuild(applied);   // commit new params; skip sync rebuild
        invalidateProcessedCache();      // schedule async re-pipeline
    } else {
        m_view->setParams(applied);
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
