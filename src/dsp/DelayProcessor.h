#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

#include "Common.h"
#include "Constants.h"
#include "Dd8Delay.h"
#include "DelayLine.h"
#include "EffectBase.h"

namespace namx
{

// Stereo delay: models of a Boss DD-8's Standard, Analog, Tape, Warm and Mod modes
// (Dd8Delay.h), the pedal's clip, filters, Feedback map and LFO on NAM-X's two lines.
// Mono in + mono mode: one delay line.
// Mono in + stereo mode: the DD-8's own stereo (measured 2026-11-10, captures/boss/dd-8,
//   Stereo): one loop, heard twice. A repeat comes on the left Time after the input and on
//   the right R Time after it (the same repeat: the same level and tone); Feedback, the
//   loop's filters and the clip act once per round, the round as long as the longer time.
//   R Time = twice Time is the pedal: the repeats alternate sides in equal pairs and last
//   about twice as long as in mono. Equal times repeat on both sides together.
// Stereo in: each channel through its own line — L/R times in stereo mode, both at
//   Time in mono mode (R Time is only used in stereo mode).
// Mix is the pedal's Level (an audio taper, the dry stays at unity), Feedback 0–90 % is the
// pedal's 7:00–5:00; Low/High Cut act inside the feedback loop, so each repeat is darker
// than the last.
class DelayProcessor : public EffectBase
{
public:
    // Type values (preset key delay/type): append only. 0 was NAM-X's own digital delay,
    // removed 2026-11-03 for the DD-8: a preset with it loads as DD-8 Standard.
    enum Type : int
    {
        DD8_STANDARD = 1,
        DD8_ANALOG = 2,
        DD8_TAPE = 3,
        DD8_WARM = 4,
        DD8_MOD = 5,
    };
    static constexpr int FIRST_TYPE = DD8_STANDARD;
    static constexpr int LAST_TYPE = DD8_MOD;

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
    void set_level_db(float db) {
        level_db.store(db);
    }
    // The right line's own delay (stereo mode, stereo input); the mono line uses Time.
    void set_right_time(float ms) {
        right_ms.store(ms);
    }
    void set_type(int new_type) {
        type.store(std::clamp(new_type, FIRST_TYPE, LAST_TYPE));
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
    float get_level_db() const {
        return level_db.load();
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
        // The voicing's chains and loop low-pass.
        BiquadChainState<dd8::CHAIN> loop_chain, out_chain, post_chain, feedback_chain;
        float loop_lp = 0.0f;
        // The feedback path's own latency (dd8::FEEDBACK_DELAY_MS): a short ring.
        std::array<float, 256> back_ring{};
        size_t back_pos = 0;
        float delayed(float x, size_t n) {
            back_ring[back_pos] = x;
            const float y = back_ring[(back_pos + back_ring.size() - n) % back_ring.size()];
            back_pos = (back_pos + 1) % back_ring.size();
            return y;
        }
        // Read before this sample's push(), so "d samples ago" is d - 1 behind the
        // newest sample in the line.
        float read(float delay_samples) const {
            return samples.interpolate_cubic(delay_samples - 1.0f);
        }
        void push(float x) {
            samples.push(x);
        }
    };

    // One block's settings, from the knobs and the voicing.
    struct Block
    {
        const dd8::Voicing* voicing;
        const dd8::Coefficients* k;
        float fb, level, repeat, ceiling, line_limit;
        float lfo_inc, lfo_depth, flutter_inc, flutter_depth; // per sample, samples
        int wander_tick; // samples (0: no wander)
        float wander_edge, wander_max; // one-pole coefficient, samples
        float del_l, del_r; // samples
        size_t back_delay; // samples
    };
    // One line's tap: the wet it adds to the output and what goes back into the loop.
    struct Tap
    {
        float wet, back;
    };
    Block block_settings() const;
    float modulation(const Block& b); // advances the LFOs
    Tap tap(Line& l, float read_delay, const Block& b) const;
    float heard(Line& l, float r, const Block& b) const; // a repeat: clip, out chain, Level
    static float into_line(float x, const Block& b); // the line's limit
    void type_edge(int current_type);

    atomic<float> time_ms{250.0f};
    atomic<float> feedback_pct{40.0f};
    atomic<float> level_db{static_cast<float>(NAMConstants::DELAY_LEVEL_DEFAULT)};
    atomic<float> right_ms{375.0f};
    atomic<int> type{DD8_STANDARD};
    int last_type = DD8_STANDARD; // audio thread
    std::array<dd8::Coefficients, LAST_TYPE - FIRST_TYPE + 1> coefficients{}; // per type
    float lfo_phase = 0.0f, flutter_phase = 0.0f;
    // Tape's wander (dd8::WANDER_*): the delay's offset (samples), its slope now and the one
    // it is heading for (samples per sample), the samples left in this tick, and the draws.
    float wander_offset = 0.0f, wander_slope = 0.0f, wander_target = 0.0f;
    int wander_left = 0;
    uint32_t wander_seed = WANDER_SEED;
    static constexpr uint32_t WANDER_SEED = 0x2545F491u;
    float wander(const Block& b);

    Line lines[2];

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
