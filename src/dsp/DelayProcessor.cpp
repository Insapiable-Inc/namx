#include "DelayProcessor.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Common.h"

namespace namx
{

using dd8::VOICINGS;
static_assert(VOICINGS.size() == DelayProcessor::LAST_TYPE - DelayProcessor::FIRST_TYPE + 1);

void DelayProcessor::on_sample_rate_changed(double sr) {
    const size_t n = static_cast<size_t>(MAX_DELAY_MS * sr / 1000.0) + 4;
    for (auto& l : lines) {
        l.samples.resize(n);
    }
    glide_k = 1.0f - std::exp(-1.0f / (0.08f * static_cast<float>(sr)));
    for (size_t i = 0; i < VOICINGS.size(); ++i) {
        coefficients[i].prepare(*VOICINGS[i], sr);
    }
    reset();
}

void DelayProcessor::sync_right_channel() {
    // The right line takes the left's history (the repeats keep going) and glides
    // from there to its own delay time.
    if (!lines[0].samples.empty()) {
        lines[1].samples.copy_from(lines[0].samples);
    }
    lines[1].loop_chain = lines[0].loop_chain;
    lines[1].out_chain = lines[0].out_chain;
    lines[1].post_chain = lines[0].post_chain;
    lines[1].feedback_chain = lines[0].feedback_chain;
    lines[1].loop_lp = lines[0].loop_lp;
    lines[1].back_ring = lines[0].back_ring;
    lines[1].back_pos = lines[0].back_pos;
    del_now[1] = del_now[0];
}

void DelayProcessor::reset() {
    for (auto& l : lines) {
        l.samples.clear();
        l.loop_chain.reset();
        l.out_chain.reset();
        l.post_chain.reset();
        l.feedback_chain.reset();
        l.loop_lp = 0.0f;
        l.back_ring.fill(0.0f);
    }
    lfo_phase = flutter_phase = 0.0f;
    wander_offset = wander_slope = wander_target = 0.0f;
    wander_left = 0;
    wander_seed = WANDER_SEED;
    del_now[0] = del_now[1] = -1.0f;
}

float DelayProcessor::glide(int line, float target) {
    float& d = del_now[line];
    // Within half a sample it has arrived: left to the one-pole it would stall a few samples
    // short (the step falls under a float's resolution) and read between samples for good.
    d = d < 0.0f || std::abs(target - d) < 0.5f ? target : d + (target - d) * glide_k;
    return d;
}

DelayProcessor::Block DelayProcessor::block_settings() const {
    const size_t index = static_cast<size_t>(last_type - FIRST_TYPE);
    const dd8::Voicing& v = *VOICINGS[index];
    const float fs = static_cast<float>(sample_rate);
    Block b;
    b.voicing = &v;
    b.k = &coefficients[index];
    // Feedback: the gain per round at the loop's loudest frequency (100 %: it holds). Level:
    // the first repeat re the dry at 1 kHz; its lowest setting is off.
    b.fb = std::clamp(feedback_pct.load() / 100.0f, 0.0f, 1.0f) / b.k->loop_peak;
    b.repeat = dd8::db_to_gain(v.repeat_db);
    const float level = level_db.load();
    b.level = level <= static_cast<float>(NAMConstants::DELAY_LEVEL_MIN)
                  ? 0.0f
                  : dd8::db_to_gain(level) / b.repeat;
    b.ceiling = dd8::db_to_gain(dd8::REFERENCE_DBFS + dd8::OUT_CEILING_DB);
    b.line_limit = dd8::db_to_gain(dd8::REFERENCE_DBFS + v.line_db);
    b.lfo_inc = v.lfo_hz / fs;
    b.lfo_depth = v.lfo_ms * fs / 1000.0f;
    b.flutter_inc = v.flutter_hz / fs;
    b.flutter_depth = v.flutter_ms * fs / 1000.0f;
    // The reads stay inside the line with the voicing's LFO on top.
    b.wander_tick = v.wander ? static_cast<int>(std::lround(dd8::WANDER_TICK_S * fs)) : 0;
    b.wander_edge = 1.0f - std::exp(-1000.0f / (dd8::WANDER_EDGE_MS * fs));
    b.wander_max = dd8::WANDER_MAX_MS * fs / 1000.0f;
    const float mod_depth =
        (v.lfo_ms + v.flutter_ms + (v.wander ? dd8::WANDER_MAX_MS : 0.0f)) * fs / 1000.0f;
    const float max_d = static_cast<float>(lines[0].samples.size()) - 2.0f - mod_depth;
    const float advance = v.advance_ms * fs / 1000.0f;
    b.back_delay = std::min<size_t>(
        255, static_cast<size_t>(std::lround(dd8::FEEDBACK_DELAY_MS * fs / 1000.0f)));
    b.del_l = std::clamp(time_ms.load() * fs / 1000.0f - advance, 1.0f + mod_depth, max_d);
    // Stereo off: R Time is unused (its knob is dimmed) — a stereo input runs both lines at Time.
    b.del_r = is_stereo_mode()
                  ? std::clamp(right_ms.load() * fs / 1000.0f - advance, 1.0f + mod_depth, max_d)
                  : b.del_l;
    if (mod_depth == 0.0f) {
        // A steady delay is a whole number of samples: nothing is read between samples, so a
        // repeat loses no treble to the reading however many times it goes round.
        b.del_l = std::round(b.del_l);
        b.del_r = std::round(b.del_r);
    }
    return b;
}

void DelayProcessor::type_edge(int current_type) {
    // A new voicing starts its filters and LFOs from rest; the repeats in the line stay.
    if (current_type != last_type) {
        for (auto& l : lines) {
            l.loop_chain.reset();
            l.out_chain.reset();
            l.post_chain.reset();
            l.feedback_chain.reset();
            l.loop_lp = 0.0f;
        }
        lfo_phase = flutter_phase = 0.0f;
        wander_offset = wander_slope = wander_target = 0.0f;
        wander_left = 0;
        wander_seed = WANDER_SEED;
        last_type = current_type;
    }
}

float DelayProcessor::modulation(const Block& b) {
    constexpr float TWO_PI = 2.0f * std::numbers::pi_v<float>;
    if (b.lfo_depth <= 0.0f && b.flutter_depth <= 0.0f && b.wander_tick == 0) {
        return 0.0f;
    }
    const float mod = b.lfo_depth * std::sin(TWO_PI * lfo_phase) +
                      b.flutter_depth * std::sin(TWO_PI * flutter_phase) + wander(b);
    lfo_phase += b.lfo_inc;
    lfo_phase -= lfo_phase >= 1.0f ? 1.0f : 0.0f;
    flutter_phase += b.flutter_inc;
    flutter_phase -= flutter_phase >= 1.0f ? 1.0f : 0.0f;
    return mod;
}

float DelayProcessor::wander(const Block& b) {
    if (b.wander_tick == 0) {
        return 0.0f;
    }
    if (--wander_left <= 0) {
        // A new pitch offset for this tick: one of the 16, from a fixed sequence (the same
        // every run), and a share of the way back to the centre. A pitch c cents up is the
        // delay shrinking by ln 2 / 1200 · c samples per sample.
        wander_left = b.wander_tick;
        wander_seed = wander_seed * 1664525u + 1013904223u;
        const float cents = dd8::WANDER_CENTS[wander_seed >> 28];
        wander_target = -cents * (std::numbers::ln2_v<float> / 1200.0f) -
                        dd8::WANDER_RETURN * wander_offset / static_cast<float>(b.wander_tick);
    }
    wander_slope += b.wander_edge * (wander_target - wander_slope);
    wander_offset = std::clamp(wander_offset + wander_slope, -b.wander_max, b.wander_max);
    return wander_offset;
}

float DelayProcessor::into_line(float x, const Block& b) {
    // The line's own limit (hard, or Analog's soft saturation).
    return dd8::clip(x, b.line_limit, b.voicing->line_knee);
}

float DelayProcessor::heard(Line& l, float r, const Block& b) const {
    // A repeat on its way out: the mode's tone and gain, the clip, then Level.
    const float shaped = b.repeat * l.out_chain.process(r, b.k->out);
    return b.level * l.post_chain.process(dd8::clip(shaped, b.ceiling, 0.0f), b.k->post);
}

DelayProcessor::Tap DelayProcessor::tap(Line& l, float read_delay, const Block& b) const {
    // The loop chain acts on every pass; the clip and the out chain on
    // what is heard.
    const float r = l.loop_chain.process(l.read(read_delay), b.k->loop);
    l.loop_lp += b.k->loop_low_pass * (r - l.loop_lp);
    return {heard(l, r, b),
            l.delayed(l.feedback_chain.process(l.loop_lp, b.k->feedback), b.back_delay)};
}

void DelayProcessor::process_block_mono(float* buf, size_t nframes) {
    if (lines[0].samples.empty()) {
        return;
    }
    type_edge(type.load());
    const Block b = block_settings();
    Line& l = lines[0];
    for (size_t i = 0; i < nframes; ++i) {
        const float rd = glide(0, b.del_l) + modulation(b);
        const Tap t = tap(l, rd, b);
        l.push(into_line(buf[i] + b.fb * t.back, b));
        buf[i] += t.wet;
    }
}

void DelayProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    // Stereo input: independent line per channel (no cross-feedback).
    if (lines[0].samples.empty()) {
        return;
    }
    type_edge(type.load());
    const Block b = block_settings();
    for (size_t i = 0; i < nframes; ++i) {
        const float mod = modulation(b);
        const Tap t_l = tap(lines[0], glide(0, b.del_l) + mod, b);
        const Tap t_r = tap(lines[1], glide(1, b.del_r) - mod, b);
        lines[0].push(into_line(l[i] + b.fb * t_l.back, b));
        lines[1].push(into_line(r[i] + b.fb * t_r.back, b));
        l[i] += t_l.wet;
        r[i] += t_r.wet;
    }
}

void DelayProcessor::process_block_stereo_out(float* l, float* r, size_t nframes) {
    // The DD-8 with both outputs connected: one loop, heard twice. A repeat comes on the left
    // Time after the input and on the right R Time after it — the same repeat, the same level
    // and tone — and the later of the two is what Feedback sends back, so the
    // loop is as long as the longer time. R Time = twice Time is the pedal (left, right, left…
    // evenly); equal times repeat on both sides together.
    if (lines[0].samples.empty()) {
        return;
    }
    type_edge(type.load());
    const Block b = block_settings();
    Line& loop = lines[0]; // the one line; lines[1] only lends the right tap its filters
    Line& right = lines[1];
    const bool right_last = b.del_r >= b.del_l;
    for (size_t i = 0; i < nframes; ++i) {
        const float x = l[i];
        const float mod = modulation(b);
        // The loop chain on each tap: the same filter on the same signal.
        const float on_left =
            loop.loop_chain.process(loop.read(glide(0, b.del_l) + mod), b.k->loop);
        const float on_right =
            right.loop_chain.process(loop.read(glide(1, b.del_r) - mod), b.k->loop);
        loop.loop_lp += b.k->loop_low_pass * ((right_last ? on_right : on_left) - loop.loop_lp);
        const float back =
            loop.delayed(loop.feedback_chain.process(loop.loop_lp, b.k->feedback), b.back_delay);
        loop.push(into_line(x + b.fb * back, b));
        l[i] = x + heard(loop, on_left, b);
        r[i] = r[i] + heard(right, on_right, b);
    }
}

} // namespace namx
