#include "ChorusProcessor.h"

#include <algorithm>
#include <cmath>

#include "Common.h"

namespace npl
{

static constexpr float PI = 3.14159265358979323846f;

void ChorusProcessor::on_sample_rate_changed(double sr) {
    const size_t length = static_cast<size_t>((BASE_MS + MAX_DEPTH_MS + 5.0f) * sr / 1000.0f) + 4;
    line_l.resize(length);
    line_r.resize(length);
    reset();
}

void ChorusProcessor::reset() {
    line_l.clear();
    line_r.clear();
    lfo_phase = 0.0f;
    vint_lp[0] = vint_lp[1] = 0.0f;
}

float ChorusProcessor::wet(const DelayLine& line, float phase, float depth, float fs,
                           int current_mode) {
    auto tap = [&](float ph) {
        float lfo;
        if (current_mode == VINTAGE) {
            const float f = ph - std::floor(ph);
            lfo = 4.0f * std::abs(f - 0.5f) - 1.0f; // triangle
        } else {
            lfo = std::sin(2.0f * PI * ph);
        }
        const float del = std::max(1.0f, (BASE_MS + depth * MAX_DEPTH_MS * lfo) * fs / 1000.0f);
        return line.interpolate(del);
    };
    if (current_mode == TRI) {
        return (tap(phase) + tap(phase + 1.0f / 3.0f) + tap(phase + 2.0f / 3.0f)) * (1.0f / 3.0f);
    }
    return tap(phase);
}

// One processing loop for every dispatch case: `stereoOut` = mono in, true
// stereo out (L/R LFOs offset); `dual` = stereo in, same LFO per channel.
static inline float vintage_tone(float x, float& lp, float k) {
    lp += k * (x - lp);
    return lp;
}

void ChorusProcessor::process_block_mono(float* buf, size_t nframes) {
    const float depth = depth_pct.load() / 100.0f;
    const float mix = mix_pct.load() / 100.0f;
    const float fs = static_cast<float>(sample_rate);
    const float ph_inc = rate_hz.load() / fs;
    const int current_mode = mode.load(std::memory_order_relaxed);
    const float lp_k = 1.0f - std::exp(-2.0f * PI * 3200.0f / fs);

    for (size_t i = 0; i < nframes; ++i) {
        line_l.push(buf[i]);
        float w = wet(line_l, lfo_phase, depth, fs, current_mode);
        if (current_mode == VINTAGE) {
            w = vintage_tone(w, vint_lp[0], lp_k);
        }
        buf[i] = buf[i] * (1.0f - mix) + w * mix;
        lfo_phase += ph_inc;
        if (lfo_phase >= 1.0f) {
            lfo_phase -= 1.0f;
        }
    }
}

void ChorusProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    const float depth = depth_pct.load() / 100.0f;
    const float mix = mix_pct.load() / 100.0f;
    const float fs = static_cast<float>(sample_rate);
    const float ph_inc = rate_hz.load() / fs;
    const int current_mode = mode.load(std::memory_order_relaxed);
    const float lp_k = 1.0f - std::exp(-2.0f * PI * 3200.0f / fs);

    for (size_t i = 0; i < nframes; ++i) {
        // Same LFO for both channels: identical mono effect applied per channel.
        line_l.push(l[i]);
        line_r.push(r[i]);
        float w_l = wet(line_l, lfo_phase, depth, fs, current_mode);
        float w_r = wet(line_r, lfo_phase, depth, fs, current_mode);
        if (current_mode == VINTAGE) {
            w_l = vintage_tone(w_l, vint_lp[0], lp_k);
            w_r = vintage_tone(w_r, vint_lp[1], lp_k);
        }
        l[i] = l[i] * (1.0f - mix) + w_l * mix;
        r[i] = r[i] * (1.0f - mix) + w_r * mix;
        lfo_phase += ph_inc;
        if (lfo_phase >= 1.0f) {
            lfo_phase -= 1.0f;
        }
    }
}

void ChorusProcessor::process_block_stereo_out(float* l, float* r, size_t nframes) {
    const float depth = depth_pct.load() / 100.0f;
    const float mix = mix_pct.load() / 100.0f;
    const float fs = static_cast<float>(sample_rate);
    const float ph_inc = rate_hz.load() / fs;
    const int current_mode = mode.load(std::memory_order_relaxed);
    const float lp_k = 1.0f - std::exp(-2.0f * PI * 3200.0f / fs);
    const float off = current_mode == SPATIAL ? 0.25f : 0.5f; // LFO offset
    const float r_sign = current_mode == SPATIAL ? -1.0f : 1.0f; // Spatial: inverted right wet

    for (size_t i = 0; i < nframes; ++i) {
        line_l.push(l[i]);
        line_r.push(r[i]);
        float w_l = wet(line_l, lfo_phase, depth, fs, current_mode);
        float w_r = wet(line_r, lfo_phase + off, depth, fs, current_mode);
        if (current_mode == VINTAGE) {
            w_l = vintage_tone(w_l, vint_lp[0], lp_k);
            w_r = vintage_tone(w_r, vint_lp[1], lp_k);
        }
        l[i] = l[i] * (1.0f - mix) + w_l * mix;
        r[i] = r[i] * (1.0f - mix) + r_sign * w_r * mix;
        lfo_phase += ph_inc;
        if (lfo_phase >= 1.0f) {
            lfo_phase -= 1.0f;
        }
    }
}

} // namespace npl
