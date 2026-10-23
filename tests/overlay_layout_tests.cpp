#include "../source/OverlayLayout.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

static bool Near(float x, float y) { return std::fabs(x - y) < 0.0001f; }

int main() {
    const auto hd = cr::ComputeOverlayLayout(1920, 1080, 96, 1.0f);
    const auto fourK = cr::ComputeOverlayLayout(3840, 2160, 96, 1.0f);
    assert(hd.fontX > 0.32f && hd.fontY > 0.65f);
    assert(Near(fourK.fontX, hd.fontX * 2.0f));
    assert(Near(fourK.fontY, hd.fontY * 2.0f));
    assert(Near(fourK.x, hd.x * 2.0f));
    assert(Near(fourK.rowHeight, hd.rowHeight * 2.0f));
    assert(fourK.wrapX < 3840 && fourK.wrapX > 3600);

    // DPI scaling does not double-apply when the framebuffer is already 4K.
    const auto fourKDpi = cr::ComputeOverlayLayout(3840, 2160, 192, 1.0f);
    assert(Near(fourK.fontY, fourKDpi.fontY));
    const auto hdDpi = cr::ComputeOverlayLayout(1920, 1080, 192, 1.0f);
    assert(Near(hdDpi.fontY, hd.fontY * 2.0f));
    const auto manual = cr::ComputeOverlayLayout(3840, 2160, 192, 1.5f);
    assert(Near(manual.fontX, fourK.fontX * 1.5f));
    const auto clamped = cr::ComputeOverlayLayout(3840, 2160, 96, 99.0f);
    assert(Near(clamped.fontX, fourK.fontX * 2.5f));

    // Size changes, malformed values and legacy 4:3 remain bounded.
    const auto sd = cr::ComputeOverlayLayout(640, 448, 96, 1.0f);
    assert(sd.fontX > 0 && sd.fontY > 0 && sd.firstY > 0);
    assert(sd.wrapX < 640 && sd.wrapX > sd.x);
    const auto invalid = cr::ComputeOverlayLayout(0, -1, 0, std::numeric_limits<float>::quiet_NaN());
    assert(std::isfinite(invalid.fontX) && invalid.fontX > 0);
    assert(std::isfinite(invalid.fontY) && invalid.fontY > 0);
    std::cout << "overlay_layout_tests: PASS\n";
}
