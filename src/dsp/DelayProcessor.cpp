#include "DelayProcessor.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Common.h"

namespace namx
{

void DelayProcessor::on_sample_rate_changed(double sr) {
    const size_t n = static_cast<size_t>(MAX_DELAY_MS * sr / 1000.0) + 4;
    for (auto& l : lines) {
        l.samples.resize(n);
    }
    for (auto& s : shift) {
        s.prepare(sr, 12.0f);
    }
    glide_k = 1.0f - std::exp(-1.0f / (0.08f * static_cast<float>(sr)));
    reset();
}

void DelayProcessor::sync_right_channel() {
    // The right line takes the left's history (the repeats keep going) and glides
    // from there to its own delay time; its shifter starts over.
    if (!lines[0].samples.empty()) {
        lines[1].samples.copy_from(lines[0].samples);
    }
    lines[1].hp_x = lines[0].hp_x;
    lines[1].hp_y = lines[0].hp_y;
    lines[1].lp = lines[0].lp;
    del_now[1] = del_now[0];
    shift[1].reset();
}

void DelayProcessor::reset() {
    for (auto& l : lines) {
        l.samples.clear();
        l.hp_x = l.hp_y = l.lp = 0.0f;
    }
    mod_phase = 0.0f;
    del_now[0] = del_now[1] = -1.0f;
    for (auto& s : shift) {
        s.reset();
    }
}

float DelayProcessor::glide(int line, float target) {
    float& d = del_now[line];
    d = d < 0.0f ? target : d + (target - d) * glide_k; // exact once it has arrived
    return d;
}

float DelayProcessor::loop(int line, float d, float read_delay, const Params& p) {
    if (p.shimmer <= 0.0f) {
        return d;
    }
    // A repeat shorter than the shifter's latency can't be compensated: its octave
    // arrives late (and it's fed the repeat itself).
    const float early_delay = read_delay - static_cast<float>(shift[line].latency_samples());
    const float early = early_delay >= 1.0f ? lines[line].read(early_delay) : d;
    return d + p.shimmer * (shift[line].process(early) - d);
}

// The shifters only run while Shimmer is up: on the way back up they would replay
// the repeats they held when last on (audit 2026-10-04, Fable item 16).
void DelayProcessor::shimmer_edge(float shimmer) {
    if (shimmer > 0.0f && !shim_on) {
        shift[0].reset();
        shift[1].reset();
    }
    shim_on = shimmer > 0.0f;
}

DelayProcessor::Params DelayProcessor::params() const {
    const float fs = static_cast<float>(sample_rate);
    Params p;
    p.fb = std::min(feedback_pct.load() / 100.0f, 0.99f);
    p.mix = mix_pct.load() / 100.0f;
    p.cross = std::clamp(cross_pct.load(), 0.0f, 100.0f) / 100.0f;
    p.hp_a = std::exp(
        -(2.0f * std::numbers::pi_v<float>)*std::clamp(low_cut_hz.load(), 20.0f, 2000.0f) / fs);
    p.lp_k = 1.0f - std::exp(-(2.0f * std::numbers::pi_v<float>)*std::clamp(high_cut_hz.load(),
                                                                            500.0f, 0.45f * fs) /
                             fs);
    p.sat_drive = std::clamp(sat_pct.load(), 0.0f, 100.0f) / 100.0f;
    p.mod_depth = 0.003f * fs * std::clamp(mod_pct.load(), 0.0f, 100.0f) / 100.0f; // up to ±3 ms
    p.ph_inc = 0.55f / fs; // 0.55 Hz wobble
    p.shimmer = std::clamp(shimmer_pct.load(), 0.0f, 100.0f) / 100.0f;
    const float max_d = static_cast<float>(lines[0].samples.size()) - 2.0f - p.mod_depth;
    p.del_l = std::clamp(time_ms.load() * fs / 1000.0f, 1.0f + p.mod_depth, max_d);
    // Stereo off: R Time is unused (its knob is dimmed) — a stereo input runs both lines at Time.
    p.del_r = is_stereo_mode()
                  ? std::clamp(right_ms.load() * fs / 1000.0f, 1.0f + p.mod_depth, max_d)
                  : p.del_l;
    return p;
}

float DelayProcessor::tone(Line& l, float x, const Params& p) const {
    const float hp = p.hp_a * (l.hp_y + x - l.hp_x);
    l.hp_x = x;
    l.hp_y = hp;
    l.lp += p.lp_k * (hp - l.lp);
    return l.lp;
}

float DelayProcessor::saturate(float x, float amount) {
    if (amount <= 0.0f) {
        return x;
    }
    // Blend toward a soft clip (Padé tanh): unity small-signal gain, loud
    // repeats round off and compress — tape-like, never harsher.
    const float v = std::clamp(x, -3.0f, 3.0f);
    const float t = v * (27.0f + v * v) / (27.0f + 9.0f * v * v);
    return x + amount * (t - x);
}

void DelayProcessor::process_block_mono(float* buf, size_t nframes) {
    if (lines[0].samples.empty()) {
        return;
    }
    const Params p = params();
    shimmer_edge(p.shimmer);
    Line& l = lines[0];
    for (size_t i = 0; i < nframes; ++i) {
        const float mod = p.mod_depth * std::sin((2.0f * std::numbers::pi_v<float>)*mod_phase);
        mod_phase += p.ph_inc;
        if (mod_phase >= 1.0f) {
            mod_phase -= 1.0f;
        }

        const float rd = glide(0, p.del_l) + mod;
        const float d = tone(l, l.read(rd), p);
        l.push(saturate(buf[i] + p.fb * loop(0, d, rd, p), p.sat_drive));
        buf[i] = buf[i] * (1.0f - p.mix) + d * p.mix;
    }
}

void DelayProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    // Stereo input: independent line per channel (no cross-feedback).
    if (lines[0].samples.empty()) {
        return;
    }
    const Params p = params();
    shimmer_edge(p.shimmer);
    for (size_t i = 0; i < nframes; ++i) {
        const float mod = p.mod_depth * std::sin((2.0f * std::numbers::pi_v<float>)*mod_phase);
        mod_phase += p.ph_inc;
        if (mod_phase >= 1.0f) {
            mod_phase -= 1.0f;
        }

        const float rd_l = glide(0, p.del_l) + mod, rd_r = glide(1, p.del_r) - mod;
        const float d_l = tone(lines[0], lines[0].read(rd_l), p);
        const float d_r = tone(lines[1], lines[1].read(rd_r), p);
        lines[0].push(saturate(l[i] + p.fb * loop(0, d_l, rd_l, p), p.sat_drive));
        lines[1].push(saturate(r[i] + p.fb * loop(1, d_r, rd_r, p), p.sat_drive));
        l[i] = l[i] * (1.0f - p.mix) + d_l * p.mix;
        r[i] = r[i] * (1.0f - p.mix) + d_r * p.mix;
    }
}

void DelayProcessor::process_block_stereo_out(float* l, float* r, size_t nframes) {
    // Cross-feed c: 1 = ping-pong (input → left only, repeats alternate),
    // 0 = two independent lines (input into both, L/R times).
    if (lines[0].samples.empty()) {
        return;
    }
    const Params p = params();
    shimmer_edge(p.shimmer);
    const float c = p.cross;
    for (size_t i = 0; i < nframes; ++i) {
        const float mod = p.mod_depth * std::sin((2.0f * std::numbers::pi_v<float>)*mod_phase);
        mod_phase += p.ph_inc;
        if (mod_phase >= 1.0f) {
            mod_phase -= 1.0f;
        }

        const float x = l[i];
        const float rd_l = glide(0, p.del_l) + mod, rd_r = glide(1, p.del_r) - mod;
        const float d_l = tone(lines[0], lines[0].read(rd_l), p);
        const float d_r = tone(lines[1], lines[1].read(rd_r), p);
        const float f_l = loop(0, d_l, rd_l, p), f_r = loop(1, d_r, rd_r, p);
        lines[0].push(saturate(x + p.fb * ((1.0f - c) * f_l + c * f_r), p.sat_drive));
        lines[1].push(saturate((1.0f - c) * x + p.fb * ((1.0f - c) * f_r + c * f_l), p.sat_drive));
        l[i] = x * (1.0f - p.mix) + d_l * p.mix;
        r[i] = r[i] * (1.0f - p.mix) + d_r * p.mix;
    }
}

} // namespace namx
