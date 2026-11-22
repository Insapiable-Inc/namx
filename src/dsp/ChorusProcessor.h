#pragma once

#include <atomic>

#include "Ce5Chorus.h"
#include "Common.h"
#include "Constants.h"
#include "DelayLine.h"
#include "EffectBase.h"

namespace namx
{

// Chorus: a model of a Boss CE-5 Chorus Ensemble (Ce5Chorus.h). Level is the chorus re the
// dry in dB (the pedal's E.Level range, with Off at the bottom; the dry always stays), Depth
// the sweep's width on a plain curve over the pedal's range, Rate its Rate in Hz (the knob
// follows the pedal's curve, ce5::RATE_HZ), Low Cut and High Cut its filter knobs in Hz. Mono is the
// pedal's mono output: the dry and one chorus. Stereo mode: the mono pedal on each side, the
// right side's sweep half a cycle behind the left's (one delay line, read twice). A stereo
// input runs the mono pedal on each channel, with one LFO.
class ChorusProcessor : public EffectBase
{
public:
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
    void set_level_db(float db) {
        level_db.store(db);
    }
    // The chorus's low and high cut, Hz (CHORUS_LOW_CUT_MIN and CHORUS_HIGH_CUT_MAX: off).
    void set_low_cut_hz(float hz) {
        low_cut_hz.store(hz);
    }
    void set_high_cut_hz(float hz) {
        high_cut_hz.store(hz);
    }

    float get_rate_hz() const {
        return rate_hz.load();
    }
    float get_depth_pct() const {
        return depth_pct.load();
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
    atomic<float> rate_hz{1.0f};
    atomic<float> depth_pct{static_cast<float>(NAMConstants::CHORUS_DEPTH_DEFAULT)};
    atomic<float> level_db{static_cast<float>(NAMConstants::CHORUS_LEVEL_DEFAULT)};
    atomic<float> low_cut_hz{static_cast<float>(NAMConstants::CHORUS_LOW_CUT_DEFAULT)};
    atomic<float> high_cut_hz{static_cast<float>(NAMConstants::CHORUS_HIGH_CUT_DEFAULT)};

    // Double: a float's steps near 1 are 6e-8, 1 % of a slow sweep's increment per sample (the
    // rate came out that far off).
    double lfo_phase{0.0};
    DelayLine line_l, line_r;
    ce5::Tone tone;
    ce5::ToneState tone_state[2];
    // The right side's filters haven't run (mono blocks): they start from the left's.
    bool right_stale = false;

    // The longest delay the sweep reads, with room: the deepest swing's top is ~8.1 ms.
    static constexpr float MAX_DELAY_MS = 30.0f;

    // One block's settings, from the knobs.
    struct Block
    {
        ce5::Sweep sweep;
        float level, input_ceiling, wet_ceiling, dry_gain, fs;
        double ph_inc;
    };
    Block block_settings();
    // One channel's sample: the dry (after the input clip) and the wet.
    struct Output
    {
        float dry, wet;
    };
    Output process_sample(DelayLine& line, ce5::ToneState& state, float x, float phase,
                          const Block& b);
    // The chorus read from `line` at `phase`, through one side's filters and the wet clip.
    float read_wet(const DelayLine& line, ce5::ToneState& state, float phase, const Block& b);
    void advance_lfo(const Block& b);
};

} // namespace namx
