#include "ui/features/waterfall/processing/WaterfallPipelineRunner.h"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>

namespace dolphin::ui {

WaterfallPipelineRunner::WaterfallPipelineRunner(QObject* parent)
    : QObject(parent)
{
}

void WaterfallPipelineRunner::start(
    PingSnapshot pings,
    WaterfallParams params,
    SeabedAutoParams seabed_params,
    bool seabed_enabled,
    std::shared_ptr<const imaging::SssAmplitudeContext> context,
    Completion completion,
    Failure failure)
{
    const std::uint64_t generation = ++m_generation;
    auto* watcher = new QFutureWatcher<WaterfallPipelineResult>(this);
    connect(watcher, &QFutureWatcher<WaterfallPipelineResult>::finished,
            watcher,
            [this, watcher, generation, pings,
             completion = std::move(completion), failure = std::move(failure)]() mutable {
        watcher->deleteLater();
        if (generation != m_generation) return;
        try {
            completion(std::move(pings), watcher->result());
        } catch (...) {
            if (failure) failure();
        }
    });
    watcher->setFuture(QtConcurrent::run(
        [pings, params = std::move(params),
         seabed_params = std::move(seabed_params), seabed_enabled,
         context = std::move(context)] {
            return runWaterfallPipeline(
                *pings, params, seabed_params, seabed_enabled, context.get());
        }));
}

void WaterfallPipelineRunner::cancel()
{
    ++m_generation;
}

} // namespace dolphin::ui
