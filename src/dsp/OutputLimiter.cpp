#include "OutputLimiter.h"

#include <algorithm>
#include <cmath>

#include "Common.h"

namespace namx
{

static constexpr float CEILING = 0.96605f; // −0.3 dBFS

void OutputLimiter::set_sample_rate(double sr) {
    look = std::max<size_t>(1, static_cast<size_t>(sr * 0.001));
    look_ms.store(static_cast<double>(look) / sr * 1000.0);
    delay_l.assign(look, 0.0f);
    delay_r.assign(look, 0.0f);
    q_gain.assign(look + 1, 1.0f);
    q_at.assign(look + 1, 0);
    q_head = q_count = 0;
    sample_index = 0;
    pos = 0;
    gain = 1.0f;
    release = std::exp(-1.0f / (0.080f * static_cast<float>(sr)));
    // Reach the target well within the look-ahead (5 time constants).
    attack = 1.0f - std::exp(-5.0f / static_cast<float>(look));
}

void OutputLimiter::process(float* l, float* r, size_t n) {
    const bool on = enabled.load(std::memory_order_relaxed);
    if (!on) {
        if (was_enabled) // flush the look-ahead so re-enabling doesn't replay old audio
        {
            std::fill(delay_l.begin(), delay_l.end(), 0.0f);
            std::fill(delay_r.begin(), delay_r.end(), 0.0f);
            q_head = q_count = 0;
            sample_index = 0;
            gain = 1.0f;
        }
        was_enabled = false;
        return;
    }
    was_enabled = true;
    if (delay_l.empty()) {
        return;
    }

    float min_gain = 1.0f;
    for (size_t i = 0; i < n; ++i) {
        const float peak = std::max(std::abs(l[i]), std::abs(r[i]));
        const float need = peak > CEILING ? CEILING / peak : 1.0f;

        // Write the new sample into the look-ahead; the oldest comes out.
        const float out_l = delay_l[pos], out_r = delay_r[pos];
        delay_l[pos] = l[i];
        delay_r[pos] = r[i];
        if (++pos == look) {
            pos = 0;
        }

        // Lowest gain any sample in the window (the next 1 ms) will need: drop the
        // queued gains this one undercuts (they can never be the minimum again),
        // append it, then drop the head once it has left the window.
        const size_t cap = q_gain.size();
        while (q_count > 0 && q_gain[(q_head + q_count - 1) % cap] >= need) {
            --q_count;
        }
        const size_t tail = (q_head + q_count) % cap;
        q_gain[tail] = need;
        q_at[tail] = sample_index;
        ++q_count;
        if (sample_index >= look && q_at[q_head] <= sample_index - look) {
            q_head = (q_head + 1) % cap;
            --q_count;
        }
        ++sample_index;
        const float target = q_gain[q_head];

        gain = target < gain ? gain + attack * (target - gain) : target + release * (gain - target);
        // Safety: never let a sample out above the ceiling.
        const float g =
            std::min(gain, CEILING / std::max({std::abs(out_l), std::abs(out_r), CEILING}));
        l[i] = out_l * g;
        r[i] = out_r * g;
        min_gain = std::min(min_gain, g);
    }
    const float gr_db = -20.0f * std::log10(std::max(min_gain, 1e-4f));
    if (gr_db > gr_peak_db.load(std::memory_order_relaxed)) {
        gr_peak_db.store(gr_db, std::memory_order_relaxed);
    }
}

} // namespace namx
