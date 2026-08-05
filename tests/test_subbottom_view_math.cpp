#include "ui/features/subbottom/SubBottomViewMath.h"

#include <cassert>
#include <cmath>

int main()
{
    using dolphin::ui::subBottomGridIntervalPixels;
    using dolphin::ui::subBottomVisibleTimeMs;

    assert(std::abs(subBottomGridIntervalPixels(10.f, 20'000.f, 0.5f) - 100.f)
           < 1e-6f);
    assert(std::abs(subBottomVisibleTimeMs(500, 20'000.f, 0.5f) - 50.f)
           < 1e-6f);
    assert(subBottomGridIntervalPixels(10.f, 0.f, 0.5f) == 0.f);
    assert(subBottomVisibleTimeMs(500, 20'000.f, 0.f) == 0.f);
    return 0;
}
