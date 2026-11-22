#pragma once

#include <algorithm>
#include <atomic>

#include "Bf3Flanger.h"
#include "Common.h"
#include "Constants.h"
#include "DelayLine.h"
#include "EffectBase.h"

namespace namx
{

// Flanger: a model of a Boss BF-3 Flanger (Bf3Flanger.h), its Standard and Ultra modes. Manual
// is the flanged copy's delay with Depth at 0, in ms; Depth the sweep's width on a plain curve
// over the pedal's range; Feedback the pedal's Res as the gain per pass round the loop, %; Rate
// its Rate in Hz (the knob follows the pedal's curve, bf3::RATE_HZ). Stereo mode is the
// pedal's two outputs: the same flanger on each side, the right side's sweep an eighth of a
// cycle ahead. A stereo input runs the mono pedal on each channel, with one LFO.
class FlangerProcessor : public EffectBase
{
public:
    // Type values (preset key flanger/type): append only. 0 was NAM-X's own flanger, removed
    // 2026-11-03 for the BF-3: a preset with it loads as BF-3 Standard. 3 was the refit beside
    // the first Standard; since 2026-11-10 it is Standard, and a preset's 3 loads as 1.
    enum Type : int
    {
        BF3_STANDARD = 1,
        BF3_ULTRA = 2,
        BF3_STANDARD_REFIT = 3,
    };
    static constexpr int FIRST_TYPE = BF3_STANDARD;
    static constexpr int LAST_TYPE = BF3_ULTRA;

    bool supports_stereo_out() const override {
        return true;
    }
    void reset() override;
    void sync_right_channel() override;

    void set_rate_hz(float hz) {
        rate_hz.store(hz);
    }
    void set_depth_pct(float pct) {
        depth_pct.store(pct);
    }
    void set_feedback_pct(float pct) {
        feedback_pct.store(pct);
    }
    // The pedal's Manual: the delay with Depth at 0, ms.
    void set_manual_ms(float ms) {
        manual_ms.store(ms);
    }
    // Wet, ours (the pedal's balance is fixed): the pedal's output against the input, 100 %
    // the pedal alone. The pedal's output holds its own dry (at unity within 0.13 dB), so the
    // knob turns the flanged part down beside a dry that stays: not a Mix.
    void set_wet_pct(float pct) {
        wet_pct.store(pct);
    }

    void set_type(int new_type) {
        type.store(new_type == BF3_STANDARD_REFIT ? BF3_STANDARD
                                                  : std::clamp(new_type, FIRST_TYPE, LAST_TYPE));
    }
    int get_type() const {
        return type.load();
    }

    float get_rate_hz() const {
        return rate_hz.load();
    }
    float get_depth_pct() const {
        return depth_pct.load();
    }
    float get_feedback_pct() const {
        return feedback_pct.load();
    }

protected:
    void on_sample_rate_changed(double sr) override;
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;
    void process_block_stereo_out(float* l, float* r, size_t nframes) override;

private:
    atomic<float> rate_hz{0.3f}; // matches FLANGER_RATE_DEFAULT
    atomic<float> depth_pct{static_cast<float>(NAMConstants::FLANGER_DEPTH_DEFAULT)};
    atomic<float> feedback_pct{0.0f};
    atomic<float> manual_ms{static_cast<float>(NAMConstants::FLANGER_MANUAL_DEFAULT)};
    atomic<float> wet_pct{100.0f};
    atomic<int> type{BF3_STANDARD};

    int last_type = BF3_STANDARD; // audio thread
    DelayLine line_l, line_r;
    DelayLine second_l, second_r; // ULTRA's second stage
    BiquadChain<bf3::TONE_SECTIONS> refit_tone; // the wet's tone, REFIT_TONE_DB included
    float smoothing_k = 0.0f;
    float glide_k = 0.0f; // knob changes glide over ~20 ms
    float wet_level = 1.0f; // gliding to the knob
    bf3::Lfo lfo;
    bf3::Lfo lfo_right; // stereo out: STEREO_LEAD ahead of `lfo`
    // The right side hasn't run (mono blocks) / its LFO hasn't (anything but stereo out).
    bool right_stale = false;
    bool lfo_right_stale = true;
    bf3::Sweep sweep; // gliding toward the knobs

    struct Channel
    {
        BiquadChainState<bf3::TONE_SECTIONS> out_tone; // the stage at d
        BiquadChainState<bf3::TONE_SECTIONS> second_tone; // ULTRA's stage at d/2

        void reset() {
            out_tone.reset();
            second_tone.reset();
        }
    };
    Channel channels[2];

    // One block's settings, from the knobs.
    struct Block
    {
        bf3::Sweep target;
        float fs = 48000.0f;
        double phase_step = 0.0;
        float ceiling = 1.0f;
        float dry = 1.0f;
        float wet = 1.0f;
        float res = 0.0f; // the loop's gain per pass (each of ULTRA's two)
        float half_trim = 0.0f; // samples: what ULTRA's d/2 stage reads short of half of d
        float wet_level = 1.0f; // the Wet knob, 0–1
        bool ultra = false;
    };
    Block block_settings() const;
    float next_delay_ms(const Block& b);
    // One flanger stage: the wet read `d` samples back (this sample's wet feeds the loop).
    float stage(DelayLine& line, BiquadChainState<bf3::TONE_SECTIONS>& state, float x, float d,
                const Block& b) const;
    float process_sample(DelayLine& line, DelayLine& second, Channel& ch, float x, float delay_ms,
                         const Block& b) const;
    void type_edge(int current_type);
};

} // namespace namx
