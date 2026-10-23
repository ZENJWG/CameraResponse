#pragma once

#include <algorithm>
#include <cmath>

namespace cr {

// CFont uses framebuffer pixels; scale its native 640x448 coordinates.
struct OverlayLayout {
    float x = 18.0f;
    float firstY = 24.0f;
    float rowHeight = 16.0f;
    float wrapX = 620.0f;
    float fontX = 0.32f;
    float fontY = 0.65f;
    float fontMultiplier = 1.0f;
};

inline OverlayLayout ComputeOverlayLayout(int width, int height, unsigned dpi, float userScale) {
    const float w = static_cast<float>(std::max(width, 320));
    const float h = static_cast<float>(std::max(height, 200));
    const float safeScale = std::isfinite(userScale)
        ? std::max(0.65f, std::min(userScale, 2.5f))
        : 1.0f;

    // Avoid doubling Windows DPI scaling when a high-resolution framebuffer
    // has already supplied the extra physical pixels.
    const float dpiRatio = std::max(1.0f, std::min(static_cast<float>(dpi) / 96.0f, 3.0f));
    const float renderRatio = std::max(1.0f, h / 1080.0f);
    const float dpiExtra = std::max(1.0f, dpiRatio / renderRatio);
    const float boost = 1.15f * safeScale * dpiExtra;

    OverlayLayout result;
    result.x = 18.0f * w / 640.0f;
    result.firstY = 24.0f * h / 448.0f;
    result.rowHeight = 16.0f * h / 448.0f * boost;
    result.wrapX = w - result.x;
    result.fontX = 0.32f * w / 640.0f * boost;
    result.fontY = 0.65f * h / 448.0f * boost;
    result.fontMultiplier = boost;
    return result;
}

} // namespace cr
