#pragma once

#include <atomic>

#include "Common.h"
#include "EffectBase.h"
#include "Tr2Tremolo.h"

namespace namx
{

// Tremolo: amplitude modulation. Stereo uses a 180° LFO offset (auto-pan, like the other
// modulation effects).
//
// Types (preset values — append only):
//   SINE  a sine LFO, gain 1 − depth·(1 − sin)/2 (the original NAM-X tremolo);
//   TR2   the Boss TR-2 model (Tr2Tremolo.h): Depth and Wave follow the pedal's knob maps
//         (0–100 % = 7:00–5:00), and the audio gets the pedal's 70 Hz low cut.
class TremoloProcessor : public EffectBase
{
public:
    enum Type
    {
        SINE = 0,
        TR2 = 1
    };

    bool supports_stereo_out() const override {
        return true;
    }
    void reset() override {
        phase = 0.0f;
        follower_l = follower_r = {};
        low_cut_l = low_cut_r = {};
    }

    void set_rate_hz(float hz) {
        rate_hz.store(hz);
    }
    void set_depth_pct(float pct) {
        depth_pct.store(pct);
    }
    void set_type(int t) {
        type.store(t == TR2 ? TR2 : SINE);
    }
    // The TR-2's Wave knob, 0–100 % (TR2 type only): triangle → square.
    void set_wave_pct(float pct) {
        wave_pct.store(pct);
    }

    float get_rate_hz() const {
        return rate_hz.load();
    }
    float get_depth_pct() const {
        return depth_pct.load();
    }

protected:
    void on_sample_rate_changed(double sr) override {
        tr2_coefficients.prepare(sr);
        reset();
    }
    void sync_right_channel() override {
        follower_r = follower_l;
        low_cut_r = low_cut_l;
    }
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;
    void process_block_stereo_out(float* l, float* r, size_t nframes) override;

private:
    // The TR2 type's versions of the three paths.
    void tr2_mono(float* buf, size_t nframes);
    void tr2_dual(float* l, float* r, size_t nframes);
    void tr2_stereo_out(float* l, float* r, size_t nframes);

    void advance_phase(float ph_inc) {
        phase += ph_inc;
        if (phase >= 1.0f) {
            phase -= 1.0f;
        }
    }

    atomic<float> rate_hz{5.0f};
    atomic<float> depth_pct{50.0f};
    atomic<int> type{SINE};
    atomic<float> wave_pct{50.0f};

    float phase{0.0f};
    tr2::Coefficients tr2_coefficients;
    // The right follower runs the auto-pan's offset LFO in stereo out; in dual mode both
    // channels share the left one's gain.
    tr2::Follower follower_l;
    tr2::Follower follower_r;
    tr2::LowCut low_cut_l;
    tr2::LowCut low_cut_r;
};

} // namespace namx
