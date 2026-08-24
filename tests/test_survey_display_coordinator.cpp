#include "ui/mainwindow/coordinators/SurveyDisplayCoordinator.h"
#include "ui/shared/CoordFormat.h"

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

    dolphin::ui::setCoordinateDisplayFormat(0);
    check(dolphin::ui::formatPosition(53.5, -1.25, false)
              == QStringLiteral("53.500000°N   1.250000°W"),
          "decimal-degree preference is shared");
    dolphin::ui::setCoordinateDisplayFormat(1);
    check(dolphin::ui::formatPosition(53.5, -1.25, false)
              == QStringLiteral("53° 30′ 0.00″N   1° 15′ 0.00″W"),
          "DMS preference is shared");
    check(dolphin::ui::formatPosition(6353009.4, 432458.4, true)
              == QStringLiteral("N 6353009.4 m   E 432458.4 m"),
          "projected working coordinates remain northing/easting");
    dolphin::ui::setCoordinateDisplayFormat(2);
    const QString utm = dolphin::ui::formatPosition(53.5, -1.25, false);
    check(utm.startsWith(QStringLiteral("UTM 30N"))
              && utm.contains(QStringLiteral(" E ")),
          "UTM preference projects geographic coordinates");
    const auto utm_parts = dolphin::ui::formatPositionComponents(
        53.5, -1.25, false);
    check(utm_parts.first.startsWith(QStringLiteral("UTM 30N"))
              && utm_parts.second.startsWith(QStringLiteral("E ")),
          "paired table coordinates honor UTM preference");

    std::printf("SurveyDisplayCoordinator: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
