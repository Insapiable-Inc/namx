#pragma once

#include <atomic>

#include "Common.h"
#include "EffectBase.h"
#include "Tr2Tremolo.h"

namespace namx
{

// Tremolo: a model of a Boss TR-2 (Tr2Tremolo.h). Depth and Wave are plain curves over the
// pedal's range (0–100 %: Depth linear, Wave log from a triangle to as square as the pedal gets
// at the rate), Rate is in Hz, and the audio gets the pedal's 70 Hz low cut. Stereo
// runs a second gain 180° behind the first (auto-pan, like the other modulation effects).
class TremoloProcessor : public EffectBase
{
public:
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
    // Wave, 0–100 %: triangle → square.
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
        coefficients.prepare(tr2::SUPPLY, sr);
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
    // One block's settings, from the knobs.
    struct Block
    {
        const tr2::Voicing& voicing;
        const tr2::Coefficients& k;
        float ph_inc;
        float wave_gain;
        float depth;
        float top;
    };
    Block block_settings() const;

    void advance_phase(float ph_inc) {
        phase += ph_inc;
        if (phase >= 1.0f) {
            phase -= 1.0f;
        }
    }

    atomic<float> rate_hz{5.0f};
    atomic<float> depth_pct{44.0f}; // the pedal's 12:00
    atomic<float> wave_pct{45.0f}; // the pedal's 12:00 at 5 Hz

    float phase{0.0f};
    tr2::Coefficients coefficients;
    // The right follower runs the auto-pan's offset LFO in stereo out; in dual mode both
    // channels share the left one's gain.
    tr2::Follower follower_l;
    tr2::Follower follower_r;
    tr2::LowCut low_cut_l;
    tr2::LowCut low_cut_r;
};

} // namespace namx
