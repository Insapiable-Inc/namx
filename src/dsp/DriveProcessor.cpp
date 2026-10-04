#include "DriveProcessor.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Common.h"

namespace namx
{

void DriveProcessor::sync_right_channel() {
    // The WDF network holds references between its elements, so it can't be copied:
    // a cleared clipper settles within a few samples and has no tail.
    circuit[1].reset();
    lp_per_channel[1] = lp_per_channel[0];
}

void DriveProcessor::reset() {
    for (int c = 0; c < 2; ++c) {
        lp_per_channel[c] = 0.0f;
        circuit[c].reset();
    }
}

void DriveProcessor::process_channel(float* buf, size_t n, int ch) {
    const float fs = static_cast<float>(sample_rate);
    const float drive = std::clamp(drive_pct.load(), 0.0f, 100.0f) / 100.0f;
    const float tone = std::clamp(tone_pct.load(), 0.0f, 100.0f) / 100.0f;
    const float fc = 600.0f * std::pow(10.0f, tone); // 600 Hz … 6 kHz
    const float lp_k = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * fc / fs);
    const float level = std::pow(10.0f, level_db.load() / 20.0f) * MAKEUP;

    // Digital full scale = CIRCUIT_VOLTS at the pedal's input.
    TubeScreamerClipper& c = circuit[ch];
    c.set_drive(drive);
    float lp = lp_per_channel[ch];
    for (size_t i = 0; i < n; ++i) {
        const float y = c.process(buf[i] * CIRCUIT_VOLTS) / CIRCUIT_VOLTS;
        lp += lp_k * (y - lp);
        buf[i] = lp * level;
    }
    lp_per_channel[ch] = lp;
}

} // namespace namx
