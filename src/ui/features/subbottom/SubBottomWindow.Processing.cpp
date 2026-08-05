#include "ui/features/subbottom/SubBottomWindow.h"
#include "ui/features/subbottom/SubBottomView.h"
#include "app/corrections/SubBottomCorrectionAlgorithms.h"
#include "app/display/NavCorrection.h"
#include "app/tasks/CancellationToken.h"
#include "app/tasks/OperationManager.h"

#include <memory>
#include <utility>
#include <vector>

namespace dolphin::ui {
namespace {

std::vector<core::SubBottomTrace> processTraces(
    std::vector<core::SubBottomTrace> traces,
    SbpGainParams gain,
    SbpSignalParams signal,
    app::CancellationToken cancel)
{
    return app::corrections::applySubBottomCorrections(
        traces, gain, signal, [&cancel] { return cancel.isCancelled(); })
        ? std::move(traces) : std::vector<core::SubBottomTrace>{};
}

} // namespace

void SubBottomWindow::invalidateProcessedCache()
{
    if (!m_traces_raw || m_traces_raw->empty()) return;
    scheduleProcessing();
}

void SubBottomWindow::applyGainParams(const SbpGainParams& p)
{
    m_gain_params = p;
    scheduleProcessing();
}

void SubBottomWindow::applySignalParams(const SbpSignalParams& p)
{
    m_signal_params = p;
    scheduleProcessing();
}

void SubBottomWindow::applyNavToLine(const NavProcessingParams& p)
{
    m_nav_params = p;
    scheduleProcessing();
}

void SubBottomWindow::scheduleProcessing()
{
    if (!m_traces_raw || m_traces_raw->empty()) {
        m_view->clear();
        return;
    }
    setDataState(ViewerDataState::Processing);
    m_proc_debounce->start();
}

void SubBottomWindow::onProcDebounce()
{
    if (!m_traces_raw || m_traces_raw->empty() || !m_op_mgr) return;
    setDataState(ViewerDataState::Processing);

    const SbpGainParams gain = m_gain_params;
    const SbpSignalParams signal = m_signal_params;
    const NavProcessingParams nav = m_nav_params;
    auto raw = m_traces_raw;

    m_op_mgr->run<std::vector<core::SubBottomTrace>>(
        tr("Processing sub-bottom"),
        [raw = std::move(raw), gain, signal, nav](app::CancellationToken cancel) {
            try {
                if (cancel.isCancelled()) return std::vector<core::SubBottomTrace>{};
                std::vector<core::SubBottomTrace> traces = *raw;
                if (cancel.isCancelled()) return std::vector<core::SubBottomTrace>{};
                applySbpNavCorrections(traces, nav);
                return processTraces(std::move(traces), gain, signal, cancel);
            } catch (...) {
                return std::vector<core::SubBottomTrace>{};
            }
        },
        [this](std::vector<core::SubBottomTrace> result) {
            m_view->setTraces(std::move(result));
            refreshContactOverlay();
            setDataState(ViewerDataState::Ready);
        },
        "sbpwin:proc",
        false);
}

} // namespace dolphin::ui
