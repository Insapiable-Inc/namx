#pragma once

#include <atomic>

#include "Common.h"
#include "Constants.h"
#include "DelayLine.h"
#include "EffectBase.h"
#include "StretchShifter.h"

namespace namx
{

// Stereo delay.
// Mono in + mono mode: one delay line.
// Mono in + stereo mode: two lines — left at Time, right at Time × R Time %
//   — with Cross-feed blending the repeats across: 100 % = ping-pong
//   (left → right → left …), 0 % = two independent echoes.
// Stereo in (any mode): each channel through its own line (L/R times).
// Every mode: Low/High Cut and Saturation act inside the feedback loop, so
// each repeat is darker / dirtier than the last (tape/analog character);
// Modulation wobbles the delay time (tape flutter / chorus on the repeats).
class DelayProcessor : public EffectBase
{
public:
    bool supports_stereo_out() const override {
        return true;
    }
    void reset() override;

    void set_time(float ms) {
        time_ms.store(ms);
    }
    void set_feedback_pct(float pct) {
        feedback_pct.store(pct);
    }
    void set_mix_pct(float pct) {
        mix_pct.store(pct);
    }
    void set_right_ratio(float pct) {
        right_pct.store(pct);
    } // 25–200 % of Time
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
    void set_crystals(float pct) {
        crystals_pct.store(pct);
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
        float crystals; // 0–1
    };
    Params params() const;
    float tone(Line& l, float x, const Params& p) const; // feedback-loop cuts
    // What re-enters the line: the repeat `d` (read `read_delay` samples back), blended
    // toward its octave-up (Crystals). The shifter is fed from `latency` samples earlier
    // in the line, so its output lands on the repeat instead of 150 ms after it.
    float loop(int line, float d, float read_delay, const Params& p);
    static float saturate(float x, float amount);

    atomic<float> time_ms{250.0f};
    atomic<float> feedback_pct{40.0f};
    atomic<float> mix_pct{30.0f};
    atomic<float> right_pct{100.0f};
    atomic<float> cross_pct{100.0f};
    atomic<float> low_cut_hz{20.0f};
    atomic<float> high_cut_hz{20000.0f};
    atomic<float> sat_pct{0.0f};
    atomic<float> mod_pct{0.0f};
    atomic<float> crystals_pct{0.0f};
    StretchShifter shift[2]; // +12 st, per line (Crystals)

    Line lines[2];
    float mod_phase = 0.0f;

    // Read delays actually used (samples), gliding toward the targets so a Time change
    // bends the repeats like tape instead of jumping the read position (a click, review
    // item 22). Negative = start at the target (after reset).
    float glide(int line, float target);
    float del_now[2]{-1.0f, -1.0f};
    float glide_k = 1.0f; // per-sample step, ~80 ms time constant

    // Longest Time × the longest R Time (200 %), plus headroom for the modulation.
    static constexpr float MAX_DELAY_MS =
        static_cast<float>(NAMConstants::DELAY_TIME_MAX * NAMConstants::DELAY_RIGHT_MAX / 100.0) +
        100.0f;
};

} // namespace namx
