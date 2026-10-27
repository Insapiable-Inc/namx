#pragma once

#include <algorithm>
#include <array>
#include <atomic>

#include "Common.h"
#include "Constants.h"
#include "Dd8Delay.h"
#include "DelayLine.h"
#include "EffectBase.h"
#include "StretchShifter.h"

namespace namx
{

// Stereo delay.
// Mono in + mono mode: one delay line.
// Mono in + stereo mode: two lines — left at Time, right at R Time — with Cross-feed
//   blending the repeats across: 0 % (the default; the UI has no knob) = two independent
//   echoes, 100 % = ping-pong (left → right → left …).
// Stereo in: each channel through its own line — L/R times in stereo mode, both at
//   Time in mono mode (R Time is only used in stereo mode).
// Every mode: Low/High Cut and Saturation act inside the feedback loop, so
// each repeat is darker / dirtier than the last (tape/analog character);
// Modulation wobbles the delay time (tape flutter / chorus on the repeats).
//
// Type picks the engine: Digital (the above, the default) or one of the Boss DD-8's
// modes (Dd8Delay.h), the same lines and stereo layout with the pedal's voicing — its
// clip, filters, Feedback map and LFO. There Mix is the pedal's Level (an audio taper,
// the dry stays at unity), Feedback 0–90 % is the pedal's 7:00–5:00, and Saturation,
// Modulation and Shimmer are the voicing's own (the knobs are ignored).
class DelayProcessor : public EffectBase
{
public:
    // Type values (preset key delay/type): append only.
    enum Type : int
    {
        DIGITAL = 0,
        DD8_STANDARD = 1,
        DD8_ANALOG = 2,
        DD8_TAPE = 3,
        DD8_WARM = 4,
        DD8_MOD = 5,
    };
    static constexpr int TYPE_COUNT = 6;

    bool supports_stereo_out() const override {
        return true;
    }
    void reset() override;
    void sync_right_channel() override;

    void set_time(float ms) {
        time_ms.store(ms);
    }
    void set_feedback_pct(float pct) {
        feedback_pct.store(pct);
    }
    void set_mix_pct(float pct) {
        mix_pct.store(pct);
    }
    // The right line's own delay (stereo mode, stereo input); the mono line uses Time.
    void set_right_time(float ms) {
        right_ms.store(ms);
    }
    void set_cross_feed(float pct) {
        cross_pct.store(pct);
    }
    void set_low_cut(float hz) {
        low_cut_hz.store(hz);
    }
    void set_high_cut(float hz) {
        high_cut_hz.store(hz);
    }
    void set_saturation(float pct) {
        sat_pct.store(pct);
    }
    void set_modulation(float pct) {
        mod_pct.store(pct);
    }
    // Octave-up repeats (0–100 %): each pass through the loop climbs.
    void set_shimmer_pct(float pct) {
        shimmer_pct.store(pct);
    }
    void set_type(int new_type) {
        type.store(std::clamp(new_type, 0, TYPE_COUNT - 1));
    }
    int get_type() const {
        return type.load();
    }

    float time() const {
        return time_ms.load();
    }
    float get_feedback_pct() const {
        return feedback_pct.load();
    }
    float get_mix_pct() const {
        return mix_pct.load();
    }

protected:
    void on_sample_rate_changed(double sr) override;
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;
    void process_block_stereo_out(float* l, float* r, size_t nframes) override;

private:
    struct Line
    {
        DelayLine samples;
        float hp_x = 0.0f, hp_y = 0.0f, lp = 0.0f;
        // DD-8 types: the voicing's chains and loop low-pass.
        dd8::ChainState dd8_loop, dd8_out;
        float dd8_loop_lp = 0.0f;
        // Read before this sample's push(), so "d samples ago" is d - 1 behind the
        // newest sample in the line.
        float read(float delay_samples) const {
            return samples.interpolate(delay_samples - 1.0f);
        }
        void push(float x) {
            samples.push(x);
        }
    };

    // Per-block derived values.
    struct Params
    {
        float fb, mix, cross, hp_a, lp_k, sat_drive /* 0–1 */, mod_depth, ph_inc;
        float del_l, del_r; // samples
        float shimmer; // 0–1 (octave-up repeats)
    };
    Params params() const;
    float tone(Line& l, float x, const Params& p) const; // feedback-loop cuts
    // What re-enters the line: the repeat `d` (read `read_delay` samples back), blended
    // toward its octave-up (Shimmer). The shifter is fed from `latency` samples earlier
    // in the line, so its output lands on the repeat instead of 150 ms after it.
    float loop(int line, float d, float read_delay, const Params& p);
    static float saturate(float x, float amount);

    // DD-8 types: per-block values, and one line's tap (the wet it adds to the output and
    // what goes back into the loop).
    struct Dd8Params
    {
        const dd8::Voicing* voicing;
        const dd8::Coefficients* k;
        float fb, level, ceiling;
        float lfo_inc, lfo_depth, flutter_inc, flutter_depth; // per sample, samples
    };
    struct Tap
    {
        float wet, back;
    };
    Dd8Params dd8_params(int dd8_type) const;
    float dd8_mod(const Dd8Params& q); // advances the LFOs
    Tap dd8_tap(Line& l, float read_delay, const Params& p, const Dd8Params& q) const;
    void dd8_block_mono(float* buf, size_t nframes);
    void dd8_block_dual(float* l, float* r, size_t nframes);
    void dd8_block_stereo_out(float* l, float* r, size_t nframes);
    void type_edge(int current_type);

    atomic<float> time_ms{250.0f};
    atomic<float> feedback_pct{40.0f};
    atomic<float> mix_pct{30.0f};
    atomic<float> right_ms{375.0f};
    atomic<float> cross_pct{0.0f};
    atomic<float> low_cut_hz{20.0f};
    atomic<float> high_cut_hz{20000.0f};
    atomic<float> sat_pct{0.0f};
    atomic<float> mod_pct{0.0f};
    atomic<float> shimmer_pct{0.0f};
    atomic<int> type{DIGITAL};
    int last_type = DIGITAL; // audio thread
    std::array<dd8::Coefficients, TYPE_COUNT - 1> dd8_coefficients{}; // per DD-8 type
    float dd8_lfo_phase = 0.0f, dd8_flutter_phase = 0.0f;
    StretchShifter shift[2]; // +12 st, per line (Shimmer)
    bool shim_on = false; // last block's Shimmer > 0 (audio thread)
    void shimmer_edge(float shimmer);

    Line lines[2];
    float mod_phase = 0.0f;

    // Read delays actually used (samples), gliding toward the targets so a Time change
    // bends the repeats like tape instead of jumping the read position (a click, review
    // item 22). Negative = start at the target (after reset).
    float glide(int line, float target);
    float del_now[2]{-1.0f, -1.0f};
    float glide_k = 1.0f; // per-sample step, ~80 ms time constant

    // Longest Time (either line), plus headroom for the modulation.
    static constexpr float MAX_DELAY_MS = static_cast<float>(NAMConstants::DELAY_TIME_MAX) + 100.0f;
};

} // namespace namx
