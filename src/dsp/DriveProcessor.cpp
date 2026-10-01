#include "DriveProcessor.h"

#include <algorithm>
#include <cmath>

static constexpr float K_PI = 3.14159265358979323846f;

void DriveProcessor::reset() {
    for (int c = 0; c < 2; ++c) {
        m_hp_x1[c] = m_hp_y1[c] = m_lp_per_channel[c] = 0.0f;
    }
}

// Smooth symmetric clip (Padé tanh, exact at ±3) — diode-pair-like knee.
static inline float soft_clip(float x) {
    x = std::clamp(x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

void DriveProcessor::process_channel(float* buf, size_t n, int ch) {
    const float fs = static_cast<float>(m_sample_rate);
    const float drive = std::clamp(m_drive_pct.load(), 0.0f, 100.0f) / 100.0f;
    // 0 → +6 dB, 100 % → +46 dB on the clipping path (the TS's 51k–551k range).
    const float gain = std::pow(10.0f, (6.0f + 40.0f * drive * drive) / 20.0f);
    const float tone = std::clamp(m_tone_pct.load(), 0.0f, 100.0f) / 100.0f;
    const float fc = 600.0f * std::pow(10.0f, tone); // 600 Hz … 6 kHz
    const float lp_k = 1.0f - std::exp(-2.0f * K_PI * fc / fs);
    const float level =
        std::pow(10.0f, m_level_db.load() / 20.0f) * 0.5f; // makeup for the summed path

    // One-pole high-pass at 720 Hz feeding the clipper.
    const float hp_a = std::exp(-2.0f * K_PI * 720.0f / fs);

    float x1 = m_hp_x1[ch], y1 = m_hp_y1[ch], lp = m_lp_per_channel[ch];
    for (size_t i = 0; i < n; ++i) {
        const float x = buf[i];
        const float hp = hp_a * (y1 + x - x1);
        x1 = x;
        y1 = hp;
        const float y = x + soft_clip(hp * gain);
        lp += lp_k * (y - lp);
        buf[i] = lp * level;
    }
    m_hp_x1[ch] = x1;
    m_hp_y1[ch] = y1;
    m_lp_per_channel[ch] = lp;
}
