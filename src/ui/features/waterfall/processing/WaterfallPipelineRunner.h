#pragma once

#include "ui/features/waterfall/processing/WaterfallPipeline.h"

#include <QObject>
#include <functional>
#include <memory>

namespace dolphin::ui {

// Owns latest-wins execution for widget-local pipeline rebuilds. Superseded
// results and results arriving after cancel() are discarded before delivery.
class WaterfallPipelineRunner final : public QObject {
public:
    using PingSnapshot = std::shared_ptr<const std::vector<core::SidescanPing>>;
    using Completion = std::function<void(PingSnapshot, WaterfallPipelineResult)>;
    using Failure = std::function<void()>;

    explicit WaterfallPipelineRunner(QObject* parent = nullptr);

    void start(PingSnapshot pings,
               WaterfallParams params,
               SeabedAutoParams seabed_params,
               bool seabed_enabled,
               std::shared_ptr<const imaging::SssAmplitudeContext> context,
               Completion completion,
               Failure failure = {});
    void cancel();

private:
    std::uint64_t m_generation = 0;
};

} // namespace dolphin::ui
