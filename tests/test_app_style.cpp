#include "ui/shell/AppStyle.h"
#include "ui/shell/Theme.h"

#include <QString>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line)
{
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
    }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

void checkMode(dolphin::ui::Theme::Mode mode)
{
    dolphin::ui::Theme::setMode(mode);
    const QString sheet = dolphin::ui::AppStyle::sheet();
    CHECK(!sheet.isEmpty());
    CHECK(!sheet.contains(QLatin1String("@font")));
    CHECK(!sheet.contains(QLatin1String("sans-serifBase")));
    CHECK(!sheet.contains(QLatin1String("sans-serifSm")));
    CHECK(!sheet.contains(QLatin1String("sans-serifXs")));
}

} // namespace

int main()
{
    checkMode(dolphin::ui::Theme::Mode::Dark);
    checkMode(dolphin::ui::Theme::Mode::Light);
    std::printf("AppStyle token checks: %s\n", failures == 0 ? "passed" : "failed");
    return failures == 0 ? 0 : 1;
}
