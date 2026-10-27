#include "ChorusProcessor.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Common.h"

namespace namx
{

void ChorusProcessor::on_sample_rate_changed(double sr) {
    const size_t length = static_cast<size_t>((BASE_MS + MAX_DEPTH_MS + 5.0f) * sr / 1000.0f) + 4;
    line_l.resize(length);
    line_r.resize(length);
    ce5_tone.prepare(sr);
    reset();
}

void ChorusProcessor::sync_right_channel() {
    if (!line_l.empty()) {
        line_r.copy_from(line_l);
    }
    vint_lp[1] = vint_lp[0];
    ce5_state[1] = ce5_state[0];
}

void ChorusProcessor::reset() {
    line_l.clear();
    line_r.clear();
    lfo_phase = 0.0f;
    vint_lp[0] = vint_lp[1] = 0.0f;
    ce5_state[0].reset();
    ce5_state[1].reset();
}

void ChorusProcessor::type_edge(int current_type) {
    // A new engine starts its tone filters from rest; the line and the LFO keep going.
    if (current_type != last_type) {
        ce5_state[0].reset();
        ce5_state[1].reset();
        last_type = current_type;
    }
}

ChorusProcessor::Ce5Block ChorusProcessor::ce5_block() {
    Ce5Block b;
    b.fs = static_cast<float>(sample_rate);
    const float rate = rate_hz.load();
    b.sweep.set(depth_pct.load() / 100.0f, rate);
    b.ph_inc = rate / b.fs;
    b.level = ce5::db_to_gain(ce5::knob_map(ce5::ELEVEL_DB, mix_pct.load() / 100.0f));
    b.input_ceiling = ce5::db_to_gain(ce5::REFERENCE_DBFS + ce5::INPUT_CEILING_DB);
    b.wet_ceiling = ce5::db_to_gain(ce5::REFERENCE_DBFS + ce5::WET_CEILING_DB);
    b.dry_gain = ce5::db_to_gain(ce5::DRY_GAIN_DB);
    ce5_tone.set_knobs(std::clamp(ce5_low_pct.load(), 0.0f, 100.0f) / 100.0f,
                       std::clamp(ce5_high_pct.load(), 0.0f, 100.0f) / 100.0f, sample_rate);
    return b;
}

ChorusProcessor::Ce5Out ChorusProcessor::ce5_sample(DelayLine& line, ce5::ToneState& state, float x,
                                                    float phase, const Ce5Block& b) {
    const float in = std::clamp(x, -b.input_ceiling, b.input_ceiling);
    line.push(in);
    const float delayed = line.interpolate(b.sweep.delay_ms(phase) * b.fs / 1000.0f);
    const float wet = std::clamp(b.level * delayed, -b.wet_ceiling, b.wet_ceiling);
    return {b.dry_gain * in, state.process(wet, ce5_tone)};
}

void ChorusProcessor::ce5_advance(const Ce5Block& b) {
    lfo_phase += b.ph_inc;
    if (lfo_phase >= 1.0f) {
        lfo_phase -= 1.0f;
    }
}

void ChorusProcessor::ce5_mono(float* buf, size_t nframes) {
    const Ce5Block b = ce5_block();
    for (size_t i = 0; i < nframes; ++i) {
        const Ce5Out o = ce5_sample(line_l, ce5_state[0], buf[i], lfo_phase, b);
        buf[i] = o.dry + o.wet;
        ce5_advance(b);
    }
}

void ChorusProcessor::ce5_dual(float* l, float* r, size_t nframes) {
    // Stereo input: the mono pedal on each channel, one LFO.
    const Ce5Block b = ce5_block();
    for (size_t i = 0; i < nframes; ++i) {
        const Ce5Out o_l = ce5_sample(line_l, ce5_state[0], l[i], lfo_phase, b);
        const Ce5Out o_r = ce5_sample(line_r, ce5_state[1], r[i], lfo_phase, b);
        l[i] = o_l.dry + o_l.wet;
        r[i] = o_r.dry + o_r.wet;
        ce5_advance(b);
    }
}

void ChorusProcessor::ce5_stereo_out(float* l, float* r, size_t nframes) {
    // The pedal's two outputs: A (left) the chorus alone, B (right) the dry alone.
    const Ce5Block b = ce5_block();
    for (size_t i = 0; i < nframes; ++i) {
        const Ce5Out o = ce5_sample(line_l, ce5_state[0], l[i], lfo_phase, b);
        l[i] = o.wet;
        r[i] = o.dry;
        ce5_advance(b);
    }
}

float ChorusProcessor::tap(const DelayLine& line, float phase, float depth, float fs,
                           Mode current_mode) {
    float lfo;
    if (current_mode == Mode::VINTAGE) {
        const float f = phase - std::floor(phase);
        lfo = 4.0f * std::abs(f - 0.5f) - 1.0f; // triangle
    } else {
        lfo = std::sin(2.0f * std::numbers::pi_v<float> * phase);
    }
    const float del = std::max(1.0f, (BASE_MS + depth * MAX_DEPTH_MS * lfo) * fs / 1000.0f);
    return line.interpolate(del);
}

float ChorusProcessor::wet(const DelayLine& line, float phase, float depth, float fs,
                           Mode current_mode) {
    if (current_mode == Mode::TRI) {
        return (tap(line, phase, depth, fs, current_mode) +
                tap(line, phase + 1.0f / 3.0f, depth, fs, current_mode) +
                tap(line, phase + 2.0f / 3.0f, depth, fs, current_mode)) *
               (1.0f / 3.0f);
    }
    return tap(line, phase, depth, fs, current_mode);
}

// One processing loop for every dispatch case: `stereoOut` = mono in, true
// stereo out (L/R LFOs offset); `dual` = stereo in, same LFO per channel.
static inline float vintage_tone(float x, float& lp, float k) {
    lp += k * (x - lp);
    return lp;
}

void ChorusProcessor::process_block_mono(float* buf, size_t nframes) {
    if (line_l.empty()) {
        return; // no sample rate yet (audit 2026-10-04, Fable item 17)
    }
    type_edge(type.load());
    if (last_type == CE5) {
        ce5_mono(buf, nframes);
        return;
    }
    const float depth = depth_pct.load() / 100.0f;
    const float mix = mix_pct.load() / 100.0f;
    const float fs = static_cast<float>(sample_rate);
    const float ph_inc = rate_hz.load() / fs;
    const Mode current_mode = mode.load(std::memory_order_relaxed);
    const float lp_k = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * 3200.0f / fs);

    for (size_t i = 0; i < nframes; ++i) {
        line_l.push(buf[i]);
        float w = wet(line_l, lfo_phase, depth, fs, current_mode);
        if (current_mode == Mode::VINTAGE) {
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
    if (line_l.empty()) {
        return; // no sample rate yet (audit 2026-10-04, Fable item 17)
    }
    type_edge(type.load());
    if (last_type == CE5) {
        ce5_dual(l, r, nframes);
        return;
    }
    const float depth = depth_pct.load() / 100.0f;
    const float mix = mix_pct.load() / 100.0f;
    const float fs = static_cast<float>(sample_rate);
    const float ph_inc = rate_hz.load() / fs;
    const Mode current_mode = mode.load(std::memory_order_relaxed);
    const float lp_k = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * 3200.0f / fs);

    for (size_t i = 0; i < nframes; ++i) {
        // Same LFO for both channels: identical mono effect applied per channel.
        line_l.push(l[i]);
        line_r.push(r[i]);
        float w_l = wet(line_l, lfo_phase, depth, fs, current_mode);
        float w_r = wet(line_r, lfo_phase, depth, fs, current_mode);
        if (current_mode == Mode::VINTAGE) {
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
    if (line_l.empty()) {
        return; // no sample rate yet (audit 2026-10-04, Fable item 17)
    }
    type_edge(type.load());
    if (last_type == CE5) {
        ce5_stereo_out(l, r, nframes);
        return;
    }
    const float depth = depth_pct.load() / 100.0f;
    const float mix = mix_pct.load() / 100.0f;
    const float fs = static_cast<float>(sample_rate);
    const float ph_inc = rate_hz.load() / fs;
    const Mode current_mode = mode.load(std::memory_order_relaxed);
    const float lp_k = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * 3200.0f / fs);
    const float off = current_mode == Mode::SPATIAL ? 0.25f : 0.5f; // LFO offset
    const float r_sign =
        current_mode == Mode::SPATIAL ? -1.0f : 1.0f; // Spatial: inverted right wet

    for (size_t i = 0; i < nframes; ++i) {
        line_l.push(l[i]);
        line_r.push(r[i]);
        float w_l = wet(line_l, lfo_phase, depth, fs, current_mode);
        float w_r = wet(line_r, lfo_phase + off, depth, fs, current_mode);
        if (current_mode == Mode::VINTAGE) {
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

} // namespace namx
