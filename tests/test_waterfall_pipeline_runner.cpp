#include "ui/features/waterfall/processing/WaterfallPipelineRunner.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <cassert>
#include <iostream>
#include <memory>

using namespace dolphin::ui;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    WaterfallPipelineRunner runner;
    auto pings = std::make_shared<const std::vector<dolphin::core::SidescanPing>>();
    int completions = 0;
    bool timed_out = false;
    QEventLoop loop;

    const auto completion = [&](WaterfallPipelineRunner::PingSnapshot,
                                WaterfallPipelineResult) {
        ++completions;
        loop.quit();
    };
    runner.start(pings, {}, {}, false, {}, completion);
    runner.start(pings, {}, {}, false, {}, completion);
    QTimer::singleShot(5000, &loop, [&] {
        timed_out = true;
        loop.quit();
    });
    loop.exec();
    QCoreApplication::processEvents();

    assert(!timed_out);
    assert(completions == 1);
    std::cout << "WaterfallPipelineRunner latest-wins check passed\n";
    return 0;
}
