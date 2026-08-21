#include "ui/mainwindow/coordinators/SurveyDisplayCoordinator.h"

#include <QCoreApplication>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    using Coordinator = dolphin::ui::SurveyDisplayCoordinator;
    int failures = 0;
    auto check = [&](bool value, const char* expression) {
        if (!value) {
            std::fprintf(stderr, "FAIL: %s\n", expression);
            ++failures;
        }
    };

    // First materialized line establishes selection. A completion for that same
    // line may refresh it. Every other concurrent completion stays backgrounded.
    check(Coordinator::presentationFor({}, "line-a")
              == Coordinator::Presentation::Select,
          "first line selects");
    check(Coordinator::presentationFor("line-a", "line-a")
              == Coordinator::Presentation::Select,
          "active line refresh preserves selection");
    check(Coordinator::presentationFor("line-a", "line-b")
              == Coordinator::Presentation::Background,
          "worker completion cannot steal selection");

    std::printf("SurveyDisplayCoordinator: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
