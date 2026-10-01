#include "OutputLimiter.h"

#include <algorithm>
#include <cmath>

static constexpr float K_CEILING = 0.96605f; // −0.3 dBFS

void OutputLimiter::set_sample_rate(double sr) {
    m_look = std::max<size_t>(1, static_cast<size_t>(sr * 0.001));
    m_delay_l.assign(m_look, 0.0f);
    m_delay_r.assign(m_look, 0.0f);
    m_need.assign(m_look, 1.0f);
    m_pos = 0;
    m_gain = 1.0f;
    m_release = std::exp(-1.0f / (0.080f * static_cast<float>(sr)));
    // Reach the target well within the look-ahead (5 time constants).
    m_attack = 1.0f - std::exp(-5.0f / static_cast<float>(m_look));
}

void OutputLimiter::process(float* l, float* r, size_t n) {
    const bool on = m_enabled.load(std::memory_order_relaxed);
    if (!on) {
        if (m_was_enabled) // flush the look-ahead so re-enabling doesn't replay old audio
        {
            std::fill(m_delay_l.begin(), m_delay_l.end(), 0.0f);
            std::fill(m_delay_r.begin(), m_delay_r.end(), 0.0f);
            std::fill(m_need.begin(), m_need.end(), 1.0f);
            m_gain = 1.0f;
        }
        m_was_enabled = false;
        return;
    }
    m_was_enabled = true;
    if (m_delay_l.empty()) {
        return;
    }

    float min_gain = 1.0f;
    for (size_t i = 0; i < n; ++i) {
        const float peak = std::max(std::abs(l[i]), std::abs(r[i]));
        const float need = peak > K_CEILING ? K_CEILING / peak : 1.0f;

        // Write the new sample into the look-ahead; the oldest comes out.
        const float out_l = m_delay_l[m_pos], out_r = m_delay_r[m_pos];
        m_delay_l[m_pos] = l[i];
        m_delay_r[m_pos] = r[i];
        m_need[m_pos] = need;
        if (++m_pos == m_look) {
            m_pos = 0;
        }

        // Lowest gain any sample in the window (the next 1 ms) will need.
        float target = 1.0f;
        for (float g : m_need) {
            target = std::min(target, g);
        }

        m_gain = target < m_gain ? m_gain + m_attack * (target - m_gain)
                                 : target + m_release * (m_gain - target);
        // Safety: never let a sample out above the ceiling.
        const float g =
            std::min(m_gain, K_CEILING / std::max({std::abs(out_l), std::abs(out_r), K_CEILING}));
        l[i] = out_l * g;
        r[i] = out_r * g;
        min_gain = std::min(min_gain, g);
    }
    const float gr_db = -20.0f * std::log10(std::max(min_gain, 1e-4f));
    if (gr_db > m_gr_peak_db.load(std::memory_order_relaxed)) {
        m_gr_peak_db.store(gr_db, std::memory_order_relaxed);
    }
}
