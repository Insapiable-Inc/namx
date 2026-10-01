#include "DoublerProcessor.h"

#include <algorithm>
#include <cmath>

#include "Common.h"

namespace npl
{

void DoublerProcessor::on_sample_rate_changed(double sr) {
    line.resize(static_cast<size_t>(sr * 0.032) + 4); // 20 ms + drift headroom
    reset();
}

void DoublerProcessor::reset() {
    line.clear();
    drift = drift_target = 0.0f;
    until_new_target = 0;
}

bool DoublerProcessor::process(float* l, float* r, size_t n, bool input_is_stereo) {
    if (!enabled.load() || input_is_stereo || line.empty()) {
        return input_is_stereo;
    }

    const float fs = static_cast<float>(sample_rate);
    const float spread = std::clamp(spread_ms.load(), 3.0f, 20.0f) * 0.001f * fs;
    const float mix = std::clamp(mix_pct.load(), 0.0f, 100.0f) / 100.0f;
    // Drift: a new random target every ~120 ms, approached smoothly — the
    // delay wanders ±30 % of the spread, like a second player's timing.
    const int hold = static_cast<int>(0.12f * fs);
    const float glide = 1.0f - std::exp(-1.0f / (0.25f * fs));

    for (size_t i = 0; i < n; ++i) {
        if (--until_new_target <= 0) {
            seed = seed * 1664525u + 1013904223u;
            drift_target = static_cast<float>(seed >> 8) / 8388608.0f - 1.0f;
            until_new_target = hold;
        }
        drift += glide * (drift_target - drift);

        const float x = l[i];
        line.push(x);
        const float dbl = line.interpolate(spread * (1.0f + 0.3f * drift));

        l[i] = x;
        r[i] = x * (1.0f - mix) + dbl * mix;
    }
    return true;
}

} // namespace npl
