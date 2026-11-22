#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

#include "Common.h"
#include "DelayLine.h"

namespace namx
{

// Octave-up shifter in the time domain, the classic delay-line kind: two taps read a delay
// line whose delay falls one sample per sample (so they read at twice the speed: an octave
// up) through a grain, then jumps back; each tap's grain is faded in and out with a Hann
// window, half a grain apart, so the two windows sum to one. Unlike an STFT shifter it
// doesn't need to part a note from its octave, and its latency is half a grain on average;
// its sound has the grains in it — the octave's level and phase move at the grain rate,
// spreading it into side lines a grain rate apart (what the RV-6's Shimmer octave does). For
// content below a quarter of the sample rate (twice that after the shift). Mono, one sample
// at a time; prepare() allocates, process() and reset() don't.
class GrainShifter
{
public:
    void prepare(double sr, float grain_ms) {
        grain = std::max(8.0f, grain_ms * 0.001f * static_cast<float>(sr));
        step = 1.0f / grain;
        line.resize(static_cast<size_t>(grain) + 4);
        reset();
    }

    void reset() {
        line.clear();
        phase = 0.0f;
    }

    float process(float x) noexcept {
        line.push(x);
        phase += step;
        if (phase >= 1.0f) {
            phase -= 1.0f;
        }
        float out = 0.0f;
        for (const float offset : {0.0f, 0.5f}) {
            float p = phase + offset;
            if (p >= 1.0f) {
                p -= 1.0f;
            }
            // The delay falls from a grain to none while p goes 0 → 1: read at speed 2.
            const float delay = 1.0f + grain * (1.0f - p);
            const float window = std::sin(std::numbers::pi_v<float> * p);
            out += window * window * line.interpolate(delay);
        }
        return out;
    }

    // Input to output delay of the shifted signal, on average (half a grain).
    float latency_samples() const noexcept {
        return 1.0f + 0.5f * grain;
    }

private:
    DelayLine line;
    float grain = 4800.0f, step = 1.0f / 4800.0f, phase = 0.0f;
};

} // namespace namx
