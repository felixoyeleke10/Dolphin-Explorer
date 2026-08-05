#pragma once

namespace dolphin::ui {

inline float subBottomVisibleTimeMs(int height_px,
                                    float sample_rate_hz,
                                    float pixels_per_sample)
{
    if (height_px <= 0 || sample_rate_hz <= 0.f || pixels_per_sample <= 0.f)
        return 0.f;
    const float visible_samples = static_cast<float>(height_px) / pixels_per_sample;
    return visible_samples * 1000.f / sample_rate_hz;
}

inline float subBottomGridIntervalPixels(float interval_ms,
                                         float sample_rate_hz,
                                         float pixels_per_sample)
{
    if (interval_ms <= 0.f || sample_rate_hz <= 0.f || pixels_per_sample <= 0.f)
        return 0.f;
    return interval_ms * sample_rate_hz / 1000.f * pixels_per_sample;
}

} // namespace dolphin::ui
