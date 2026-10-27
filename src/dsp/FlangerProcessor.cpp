#include "FlangerProcessor.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Common.h"
#include "Constants.h"

namespace namx
{

void FlangerProcessor::on_sample_rate_changed(double sr) {
    const float longest_ms = std::max(BASE_MS + DEPTH_MS + 5.0f, bf3::MAX_DELAY_MS);
    const size_t length = static_cast<size_t>(longest_ms * sr / 1000.0f) + 4;
    line_l.resize(length);
    line_r.resize(length);
    bf3_tone.design(bf3::TONE, sr);
    bf3_smoothing_k = bf3::smoothing_coefficient(sr);
    bf3_glide_k = static_cast<float>(1.0 - std::exp(-1.0 / (0.02 * sr)));
    reset();
}

void FlangerProcessor::sync_right_channel() {
    if (!line_l.empty()) {
        line_r.copy_from(line_l);
    }
    fb_r = fb_l;
    bf3_ch[1] = bf3_ch[0];
}

void FlangerProcessor::reset() {
    line_l.clear();
    line_r.clear();
    fb_l = fb_r = 0.0f;
    lfo_phase = 0.0f;
    bf3_lfo.reset();
    bf3_ch[0].reset();
    bf3_ch[1].reset();
    bf3_sweep = bf3_block().target;
}

void FlangerProcessor::type_edge(int current_type) {
    // A new engine starts from rest: its loop, filters and LFO, the sweep at the knobs.
    if (current_type != last_type) {
        bf3_ch[0].reset();
        bf3_ch[1].reset();
        bf3_lfo.reset();
        bf3_sweep = bf3_block().target;
        last_type = current_type;
    }
}

FlangerProcessor::Bf3Block FlangerProcessor::bf3_block() const {
    Bf3Block b;
    b.fs = static_cast<float>(sample_rate);
    const float manual = 1.0f - std::clamp(range_pct.load(), 0.0f, 100.0f) / 100.0f;
    const float depth = std::clamp(depth_pct.load(), 0.0f, 100.0f) / 100.0f;
    const float res = std::clamp(
        feedback_pct.load() / static_cast<float>(NAMConstants::FLANGER_FEEDBACK_MAX), 0.0f, 1.0f);
    b.target.set(manual, depth);
    b.phase_step = rate_hz.load() / b.fs;
    b.ceiling = bf3::db_to_gain(bf3::REFERENCE_DBFS + bf3::CEILING_DB);
    b.ultra = last_type == BF3_ULTRA;
    if (b.ultra) {
        b.dry = bf3::db_to_gain(bf3::ULTRA_DRY_DB);
        const float middle = bf3::db_to_gain(bf3::ULTRA_TAP_DB[1]);
        b.res = bf3::knob_map(bf3::ULTRA_RES, res) * middle / b.dry;
        b.taps[0] = bf3::db_to_gain(bf3::ULTRA_TAP_DB[0]);
        b.taps[1] = middle * bf3::knob_map(bf3::ULTRA_TAP2, res);
        b.taps[2] = bf3::db_to_gain(bf3::ULTRA_TAP_DB[2]) * bf3::knob_map(bf3::ULTRA_TAP3, res);
    } else {
        b.dry = bf3::db_to_gain(bf3::STANDARD_DRY_DB);
        b.wet = bf3::db_to_gain(bf3::STANDARD_WET_DB);
        b.res = bf3::knob_map(bf3::STANDARD_RES, res);
    }
    return b;
}

float FlangerProcessor::bf3_delay_ms(const Bf3Block& b) {
    bf3_sweep.floor_ms += bf3_glide_k * (b.target.floor_ms - bf3_sweep.floor_ms);
    bf3_sweep.swing_ms += bf3_glide_k * (b.target.swing_ms - bf3_sweep.swing_ms);
    return bf3_sweep.floor_ms + bf3_sweep.swing_ms * bf3_lfo.next(b.phase_step, bf3_smoothing_k);
}

float FlangerProcessor::bf3_sample(DelayLine& line, Bf3Channel& ch, float x, float delay_ms,
                                   const Bf3Block& b) const {
    const float in = x + b.res * ch.fb;
    line.push(in);
    const float d = delay_ms * b.fs / 1000.0f;
    float out;
    if (b.ultra) {
        // The dry and three taps, evenly spaced; the loop on the middle one, re-entering
        // ahead of the dry.
        const float middle = line.interpolate(d);
        ch.fb = ch.loop_tone.process(middle, bf3_tone);
        const float taps = b.taps[0] * line.interpolate(0.5f * d) + b.taps[1] * middle +
                           b.taps[2] * line.interpolate(1.5f * d);
        out = b.dry * in + ch.out_tone.process(taps, bf3_tone);
    } else {
        ch.fb = ch.out_tone.process(line.interpolate(d), bf3_tone);
        out = b.dry * x + b.wet * ch.fb;
    }
    return std::clamp(out, -b.ceiling, b.ceiling);
}

void FlangerProcessor::bf3_mono(float* buf, size_t nframes) {
    const Bf3Block b = bf3_block();
    for (size_t i = 0; i < nframes; ++i) {
        buf[i] = bf3_sample(line_l, bf3_ch[0], buf[i], bf3_delay_ms(b), b);
    }
}

void FlangerProcessor::bf3_dual(float* l, float* r, size_t nframes) {
    // Stereo input: the mono pedal on each channel, one LFO.
    const Bf3Block b = bf3_block();
    for (size_t i = 0; i < nframes; ++i) {
        const float delay_ms = bf3_delay_ms(b);
        l[i] = bf3_sample(line_l, bf3_ch[0], l[i], delay_ms, b);
        r[i] = bf3_sample(line_r, bf3_ch[1], r[i], delay_ms, b);
    }
}

void FlangerProcessor::bf3_stereo_out(float* l, float* r, size_t nframes) {
    // What the pedal's Output B carries isn't known: the mono pedal on both sides.
    const Bf3Block b = bf3_block();
    for (size_t i = 0; i < nframes; ++i) {
        l[i] = r[i] = bf3_sample(line_l, bf3_ch[0], l[i], bf3_delay_ms(b), b);
    }
}

void FlangerProcessor::process_sample(float in, float phase, DelayLine& line, float& fb,
                                      float depth, float fb_amt, float mix, float& out) const {
    float lfo = std::sin(2.0f * std::numbers::pi_v<float> * phase);
    // Range scales the sweep centre and width together (max = BASE_MS/DEPTH_MS).
    const float r = std::clamp(range_pct.load(std::memory_order_relaxed), 0.0f, 100.0f) / 100.0f;
    const float base = 1.5f + (BASE_MS - 1.5f) * r;
    const float span = 1.5f + (DEPTH_MS - 1.5f) * r;
    float delay_sam =
        std::max(1.0f, (base + depth * span * lfo) * static_cast<float>(sample_rate) / 1000.0f);

    line.push(in + fb_amt * fb);
    const float wet = line.interpolate(delay_sam);
    fb = wet;

    out = in * (1.0f - mix) + wet * mix;
}

void FlangerProcessor::process_block_mono(float* buf, size_t nframes) {
    if (line_l.empty()) {
        return; // no sample rate yet (audit 2026-10-04, Fable item 17)
    }
    type_edge(type.load());
    if (last_type != FLANGER) {
        bf3_mono(buf, nframes);
        return;
    }
    const float depth = depth_pct.load() / 100.0f;
    const float fb_amt = feedback_pct.load() / 100.0f;
    const float mix = mix_pct.load() / 100.0f;
    const float ph_inc = rate_hz.load() / static_cast<float>(sample_rate);

    for (size_t i = 0; i < nframes; ++i) {
        float out;
        process_sample(buf[i], lfo_phase, line_l, fb_l, depth, fb_amt, mix, out);
        buf[i] = out;

        lfo_phase += ph_inc;
        if (lfo_phase >= 1.0f) {
            lfo_phase -= 1.0f;
        }
    }
}

void FlangerProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    if (line_l.empty()) {
        return; // no sample rate yet (audit 2026-10-04, Fable item 17)
    }
    type_edge(type.load());
    if (last_type != FLANGER) {
        bf3_dual(l, r, nframes);
        return;
    }
    const float depth = depth_pct.load() / 100.0f;
    const float fb_amt = feedback_pct.load() / 100.0f;
    const float mix = mix_pct.load() / 100.0f;
    const float ph_inc = rate_hz.load() / static_cast<float>(sample_rate);

    for (size_t i = 0; i < nframes; ++i) {
        // Same LFO phase for both channels: identical mono effect per channel.
        float out_l, out_r;
        process_sample(l[i], lfo_phase, line_l, fb_l, depth, fb_amt, mix, out_l);
        process_sample(r[i], lfo_phase, line_r, fb_r, depth, fb_amt, mix, out_r);
        l[i] = out_l;
        r[i] = out_r;

        lfo_phase += ph_inc;
        if (lfo_phase >= 1.0f) {
            lfo_phase -= 1.0f;
        }
    }
}

void FlangerProcessor::process_block_stereo_out(float* l, float* r, size_t nframes) {
    if (line_l.empty()) {
        return; // no sample rate yet (audit 2026-10-04, Fable item 17)
    }
    type_edge(type.load());
    if (last_type != FLANGER) {
        bf3_stereo_out(l, r, nframes);
        return;
    }
    const float depth = depth_pct.load() / 100.0f;
    const float fb_amt = feedback_pct.load() / 100.0f;
    const float mix = mix_pct.load() / 100.0f;
    const float ph_inc = rate_hz.load() / static_cast<float>(sample_rate);

    for (size_t i = 0; i < nframes; ++i) {
        float out_l, out_r;
        process_sample(l[i], lfo_phase, line_l, fb_l, depth, fb_amt, mix, out_l);
        process_sample(r[i], lfo_phase + 0.5f, line_r, fb_r, depth, fb_amt, mix, out_r);
        l[i] = out_l;
        r[i] = out_r;

        lfo_phase += ph_inc;
        if (lfo_phase >= 1.0f) {
            lfo_phase -= 1.0f;
        }
    }
}

} // namespace namx
