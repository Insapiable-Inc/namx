#pragma once

#include <algorithm>
#include <atomic>

#include "Common.h"
#include "Cs3Compressor.h"
#include "EffectBase.h"

namespace namx
{

// Compressor: a model of a Boss CS-3 Compression Sustainer (Cs3Compressor.h). Threshold is the
// pedal's Sustain (as the input level that reaches its fixed ceiling; the pedal's span), Release
// its Attack knob (as the time scale of the gain's recovery, 99.5–1152 ms; the clamp-down
// follows), Level the output on top of the pedal at full Level, Tone its Tone. (Preset key
// compressor/type, from when there were two models, is ignored.)
class CompressorProcessor : public EffectBase
{
public:
    // Tone: 0–100 % = 7:00–5:00 (50 = 12:00).
    void set_tone_pct(float pct) {
        tone_pct.store(pct);
    }

    void reset() override;
    void sync_right_channel() override;

    void set_threshold(float db) {
        threshold_db.store(db);
    }
    void set_release(float ms) {
        release_ms.store(ms);
    }
    void set_makeup(float db) {
        makeup_db.store(db);
    }

    float threshold() const {
        return threshold_db.load();
    }
    float release() const {
        return release_ms.load();
    }
    float makeup() const {
        return makeup_db.load();
    }

    // Deepest gain reduction (dB, ≥ 0) since the last take — GUI meter.
    float take_gain_reduction_db() {
        return gr_peak_db.exchange(0.0f);
    }

protected:
    void on_sample_rate_changed(double /*sr*/) override {
        tone_fraction = -1.0f; // redesign at the new rate
        reset();
    }
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;

private:
    void process_channel(float* buf, size_t nframes, int ch);

    atomic<float> threshold_db{-20.26f}; // matches COMP_THRESH_DEFAULT
    atomic<float> release_ms{300.0f}; // matches COMP_RELEASE_DEFAULT
    atomic<float> makeup_db{0.0f};
    atomic<float> tone_pct{50.0f};
    atomic<float> gr_peak_db{0.0f};

    // Per-channel state: [0] = mono/L, [1] = R (stereo input).
    float env[2]{0.0f, 0.0f}; // the peak detector, linear
    BiquadChain<cs3::TONE_SECTIONS> tone;
    BiquadChainState<cs3::TONE_SECTIONS> tone_state[2];
    float tone_fraction = -1.0f; // what `tone` is designed for
};

} // namespace namx
