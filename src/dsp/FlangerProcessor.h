#pragma once

#include <algorithm>
#include <atomic>

#include "Bf3Flanger.h"
#include "Common.h"
#include "DelayLine.h"
#include "EffectBase.h"

namespace namx
{

// Flanger: short modulated delay with feedback. Stereo uses 180° LFO offset.
//
// Type picks the engine: Flanger (the above, the default) or BF-3 Standard / Ultra, a model
// of a Boss BF-3 (Bf3Flanger.h). In the BF-3 types Range is the pedal's Manual (100 % =
// fully counter-clockwise, the long end), Depth its Depth, Feedback its Res (0–99 %; a
// negative value counts as 0), Rate stays in Hz; Mix does nothing (the pedal's dry/wet
// balance is fixed). Stereo out gives the mono pedal on both sides (what its Output B
// carries isn't known).
class FlangerProcessor : public EffectBase
{
public:
    // Type values (preset key flanger/type): append only.
    enum Type : int
    {
        FLANGER = 0,
        BF3_STANDARD = 1,
        BF3_ULTRA = 2,
    };
    static constexpr int TYPE_COUNT = 3;

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
    void set_mix_pct(float pct) {
        mix_pct.store(pct);
    }
    // 0 = short, metallic sweep (≈0–3 ms) … 100 = long jet sweep (0–14 ms).
    void set_range_pct(float pct) {
        range_pct.store(pct);
    }

    void set_type(int new_type) {
        type.store(std::clamp(new_type, 0, TYPE_COUNT - 1));
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
    float get_mix_pct() const {
        return mix_pct.load();
    }

protected:
    void on_sample_rate_changed(double sr) override;
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;
    void process_block_stereo_out(float* l, float* r, size_t nframes) override;

private:
    atomic<float> rate_hz{0.3f}; // matches FLANGER_RATE_DEFAULT
    atomic<float> depth_pct{50.0f};
    atomic<float> feedback_pct{0.0f};
    atomic<float> mix_pct{50.0f};
    atomic<float> range_pct{100.0f};

    float lfo_phase{0.0f};

    DelayLine line_l, line_r;
    float fb_l{0.0f}, fb_r{0.0f};

    static constexpr float BASE_MS = 7.0f;
    static constexpr float DEPTH_MS = 7.0f;

    void process_sample(float in, float phase, DelayLine& line, float& fb, float depth,
                        float fb_amt, float mix, float& out) const;

    // BF-3 types.
    atomic<int> type{FLANGER};
    int last_type = FLANGER; // audio thread
    BiquadChain<bf3::TONE_SECTIONS> bf3_tone;
    float bf3_smoothing_k = 0.0f;
    float bf3_glide_k = 0.0f; // knob changes glide over ~20 ms
    bf3::Lfo bf3_lfo;
    bf3::Sweep bf3_sweep; // gliding toward the knobs
    struct Bf3Channel
    {
        BiquadChainState<bf3::TONE_SECTIONS> out_tone, loop_tone;
        float fb = 0.0f; // the loop's last output

        void reset() {
            out_tone.reset();
            loop_tone.reset();
            fb = 0.0f;
        }
    };
    Bf3Channel bf3_ch[2];
    struct Bf3Block
    {
        bf3::Sweep target;
        float fs = 48000.0f;
        float phase_step = 0.0f;
        float ceiling = 1.0f;
        float dry = 1.0f;
        float wet = 1.0f; // STANDARD
        float res = 0.0f; // STANDARD's per pass, ULTRA's loop (re the dry)
        float taps[3] = {}; // ULTRA's d/2, d, 3d/2 levels with Res's trim
        bool ultra = false;
    };
    Bf3Block bf3_block() const;
    float bf3_delay_ms(const Bf3Block& b);
    float bf3_sample(DelayLine& line, Bf3Channel& ch, float x, float delay_ms,
                     const Bf3Block& b) const;
    void bf3_mono(float* buf, size_t nframes);
    void bf3_dual(float* l, float* r, size_t nframes);
    void bf3_stereo_out(float* l, float* r, size_t nframes);
    void type_edge(int current_type);
};

} // namespace namx
