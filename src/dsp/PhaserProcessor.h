#pragma once

#include <algorithm>
#include <array>
#include <atomic>

#include "Common.h"
#include "EffectBase.h"
#include "Ph3Phaser.h"

namespace namx
{

// Phaser: a model of a Boss PH-3 Phase Shifter's seven modes (Ph3Phaser.h). Depth is the share
// of the sweep's travel, 0–100 % over the pedal's Depth (in Rise and Fall: how deep the notches
// are and how loud, along the pedal's path), Feedback the loop's gain per pass in % (the pedal's
// Res fully clockwise is 66–74 %, by mode), Rate in Hz down to Stop (the sweep frozen where it
// is). Stereo runs the right channel's LFO half a
// cycle behind (the pedal itself is mono). Wet is ours (the pedal's balance is fixed): the
// pedal's output against the input, 100 % the pedal alone. The pedal's output holds its own
// dry, so the knob in effect turns the phased part down beside a dry that stays: not a Mix
// (50 % is not equal parts).
class PhaserProcessor : public EffectBase
{
public:
    // Type values (preset key phaser/type): append only. 0 was the MXR Phase 90 model,
    // removed 2026-11-03 for the PH-3: a preset with it loads as 4 Stage.
    enum Type : int
    {
        PH3_4_STAGE = 1,
        PH3_8_STAGE = 2,
        PH3_10_STAGE = 3,
        PH3_12_STAGE = 4,
        PH3_RISE = 5,
        PH3_FALL = 6,
        PH3_STEP = 7,
    };
    static constexpr int FIRST_TYPE = PH3_4_STAGE;
    static constexpr int LAST_TYPE = PH3_STEP;

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
    void set_wet_pct(float pct) {
        wet_pct.store(pct);
    }
    void set_type(int new_type) {
        type.store(std::clamp(new_type, FIRST_TYPE, LAST_TYPE));
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
    atomic<float> rate_hz{0.4f};
    atomic<float> depth_pct{48.0f};
    atomic<float> feedback_pct{0.0f};
    atomic<float> wet_pct{100.0f};
    atomic<int> type{PH3_4_STAGE};

    double lfo_phase{0.0}; // a float's steps near 1 would cost a slow sweep some of its rate

    // r is null for mono; with stereo_lfo the right channel's LFO runs 180° behind.
    void render(float* l, float* r, size_t nframes, bool stereo_lfo);

    // Mono blocks advance only the left channel; the stereo-out path lines the right one up
    // first (EffectBase calls sync_right_channel only for stereo input — BUGS.md 05).
    bool right_stale = false;

    // Audio thread.
    int last_type = PH3_4_STAGE;
    std::array<ph3::Voice, 2> voices{};
    ph3::Sweep sweep;
    bool designed = false; // the voices' fixed filters match last_type and the rate
    bool fresh = true; // the next control point sets the sections at once
    float loop_k = 0.0f; // the loop's gain and the output's scale, gliding to Res
    float out_s = 1.0f;
    float glide_k = 0.0f; // ~20 ms
    float wet_level = 1.0f; // gliding to the knob
    float ladder_position = 0.0f; // Rise / Fall, in spacings (0–LADDER)
    float step_left = 0.0f; // Step: samples to the next sample-and-hold
    // Sections move at control points CONTROL samples apart, gliding in between.
    static constexpr size_t CONTROL = 16;

    void type_edge(int current_type);
    void control(int mode, size_t voice_count, float offset, size_t samples, float rate,
                 float depth);
};

} // namespace namx
