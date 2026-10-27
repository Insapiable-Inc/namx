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
    constexpr std::array<const dd8::Voicing*, TYPE_COUNT - 1> voicings{
        &dd8::STANDARD, &dd8::ANALOG, &dd8::TAPE, &dd8::WARM, &dd8::MOD};
    for (size_t i = 0; i < voicings.size(); ++i) {
        dd8_coefficients[i].prepare(*voicings[i], sr);
    }
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
    lines[1].dd8_loop = lines[0].dd8_loop;
    lines[1].dd8_out = lines[0].dd8_out;
    lines[1].dd8_loop_lp = lines[0].dd8_loop_lp;
    del_now[1] = del_now[0];
    shift[1].reset();
}

void DelayProcessor::reset() {
    for (auto& l : lines) {
        l.samples.clear();
        l.hp_x = l.hp_y = l.lp = 0.0f;
        l.dd8_loop.reset();
        l.dd8_out.reset();
        l.dd8_loop_lp = 0.0f;
    }
    mod_phase = 0.0f;
    dd8_lfo_phase = dd8_flutter_phase = 0.0f;
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
    const int current_type = type.load();
    if (current_type == DIGITAL) {
        p.mod_depth = 0.003f * fs * std::clamp(mod_pct.load(), 0.0f, 100.0f) / 100.0f; // ±3 ms
    } else {
        const dd8::Voicing& v = *dd8_params(current_type).voicing;
        p.mod_depth = (v.lfo_ms + v.flutter_ms) * fs / 1000.0f; // headroom for the voicing's LFO
    }
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

void DelayProcessor::type_edge(int current_type) {
    // A new voicing starts its filters and LFOs from rest; the repeats in the line stay.
    if (current_type != last_type) {
        for (auto& l : lines) {
            l.dd8_loop.reset();
            l.dd8_out.reset();
            l.dd8_loop_lp = 0.0f;
        }
        dd8_lfo_phase = dd8_flutter_phase = 0.0f;
        last_type = current_type;
    }
}

DelayProcessor::Dd8Params DelayProcessor::dd8_params(int dd8_type) const {
    constexpr std::array<const dd8::Voicing*, TYPE_COUNT - 1> voicings{
        &dd8::STANDARD, &dd8::ANALOG, &dd8::TAPE, &dd8::WARM, &dd8::MOD};
    const size_t index = static_cast<size_t>(std::clamp(dd8_type, 1, TYPE_COUNT - 1) - 1);
    const dd8::Voicing& v = *voicings[index];
    const float fs = static_cast<float>(sample_rate);
    Dd8Params q;
    q.voicing = &v;
    q.k = &dd8_coefficients[index];
    // Feedback 0–90 % is the pedal's knob, 7:00–5:00; Mix 0–100 % its Level.
    q.fb = dd8::knob_gain(v.feedback_db, feedback_pct.load() /
                                             static_cast<float>(NAMConstants::DELAY_FEEDBACK_MAX));
    std::array<float, 4> level_db = dd8::LEVEL_DB;
    for (float& db : level_db) {
        db += v.repeat_db;
    }
    q.level = dd8::knob_gain(level_db, mix_pct.load() / 100.0f);
    q.ceiling = dd8::db_to_gain(dd8::REFERENCE_DBFS + v.ceiling_db);
    q.lfo_inc = v.lfo_hz / fs;
    q.lfo_depth = v.lfo_ms * fs / 1000.0f;
    q.flutter_inc = v.flutter_hz / fs;
    q.flutter_depth = v.flutter_ms * fs / 1000.0f;
    return q;
}

float DelayProcessor::dd8_mod(const Dd8Params& q) {
    constexpr float TWO_PI = 2.0f * std::numbers::pi_v<float>;
    if (q.lfo_depth <= 0.0f && q.flutter_depth <= 0.0f) {
        return 0.0f;
    }
    const float mod = q.lfo_depth * std::sin(TWO_PI * dd8_lfo_phase) +
                      q.flutter_depth * std::sin(TWO_PI * dd8_flutter_phase);
    dd8_lfo_phase += q.lfo_inc;
    dd8_lfo_phase -= dd8_lfo_phase >= 1.0f ? 1.0f : 0.0f;
    dd8_flutter_phase += q.flutter_inc;
    dd8_flutter_phase -= dd8_flutter_phase >= 1.0f ? 1.0f : 0.0f;
    return mod;
}

DelayProcessor::Tap DelayProcessor::dd8_tap(Line& l, float read_delay, const Params& p,
                                            const Dd8Params& q) const {
    // The loop chain and the user's cuts act on every pass; the out chain on what is heard.
    const float r = tone(l, l.dd8_loop.process(l.read(read_delay), q.k->loop), p);
    l.dd8_loop_lp += q.k->loop_low_pass * (r - l.dd8_loop_lp);
    return {q.level * l.dd8_out.process(r, q.k->out), l.dd8_loop_lp};
}

void DelayProcessor::dd8_block_mono(float* buf, size_t nframes) {
    const Params p = params();
    const Dd8Params q = dd8_params(last_type);
    const dd8::Voicing& v = *q.voicing;
    Line& l = lines[0];
    for (size_t i = 0; i < nframes; ++i) {
        const float rd = glide(0, p.del_l) + dd8_mod(q);
        const Tap t = dd8_tap(l, rd, p, q);
        l.push(dd8::clip(buf[i] + q.fb * t.back, q.ceiling, v.knee));
        buf[i] += t.wet;
    }
}

void DelayProcessor::dd8_block_dual(float* l, float* r, size_t nframes) {
    const Params p = params();
    const Dd8Params q = dd8_params(last_type);
    const dd8::Voicing& v = *q.voicing;
    for (size_t i = 0; i < nframes; ++i) {
        const float mod = dd8_mod(q);
        const Tap t_l = dd8_tap(lines[0], glide(0, p.del_l) + mod, p, q);
        const Tap t_r = dd8_tap(lines[1], glide(1, p.del_r) - mod, p, q);
        lines[0].push(dd8::clip(l[i] + q.fb * t_l.back, q.ceiling, v.knee));
        lines[1].push(dd8::clip(r[i] + q.fb * t_r.back, q.ceiling, v.knee));
        l[i] += t_l.wet;
        r[i] += t_r.wet;
    }
}

void DelayProcessor::dd8_block_stereo_out(float* l, float* r, size_t nframes) {
    // The Digital type's layout: Cross-feed c = 1 ping-pong, 0 two independent lines.
    const Params p = params();
    const Dd8Params q = dd8_params(last_type);
    const dd8::Voicing& v = *q.voicing;
    const float c = p.cross;
    for (size_t i = 0; i < nframes; ++i) {
        const float x = l[i];
        const float mod = dd8_mod(q);
        const Tap t_l = dd8_tap(lines[0], glide(0, p.del_l) + mod, p, q);
        const Tap t_r = dd8_tap(lines[1], glide(1, p.del_r) - mod, p, q);
        lines[0].push(
            dd8::clip(x + q.fb * ((1.0f - c) * t_l.back + c * t_r.back), q.ceiling, v.knee));
        lines[1].push(dd8::clip((1.0f - c) * x + q.fb * ((1.0f - c) * t_r.back + c * t_l.back),
                                q.ceiling, v.knee));
        l[i] = x + t_l.wet;
        r[i] = r[i] + t_r.wet;
    }
}

void DelayProcessor::process_block_mono(float* buf, size_t nframes) {
    if (lines[0].samples.empty()) {
        return;
    }
    type_edge(type.load());
    if (last_type != DIGITAL) {
        shimmer_edge(0.0f);
        dd8_block_mono(buf, nframes);
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
    type_edge(type.load());
    if (last_type != DIGITAL) {
        shimmer_edge(0.0f);
        dd8_block_dual(l, r, nframes);
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
    type_edge(type.load());
    if (last_type != DIGITAL) {
        shimmer_edge(0.0f);
        dd8_block_stereo_out(l, r, nframes);
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
