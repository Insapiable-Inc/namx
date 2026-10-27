#pragma once

#include <algorithm>
#include <atomic>
#include <utility>

#include "Ce5Chorus.h"
#include "Common.h"
#include "DelayLine.h"
#include "EffectBase.h"

namespace namx
{

// Chorus: LFO-modulated delay. Modes: Normal (one voice), Tri (three voices
// 120° apart — lusher), Vintage (triangle LFO, darker BBD-style wet),
// Spatial (stereo: 90° LFOs, right wet phase-inverted for extra width).
// Stereo mode uses a 180° offset (90° in Spatial).
//
// Type picks the engine: Chorus (the above, the default) or CE-5, a model of a Boss CE-5
// (Ce5Chorus.h). In CE-5 Mix is the pedal's E.Level (the dry stays, 2.3 dB up), Depth its
// knob, Rate stays in Hz, Low and High are its filter knobs; stereo mode is the pedal's
// two outputs: the chorus left, the dry right.
class ChorusProcessor : public EffectBase
{
public:
    // Type values (preset key chorus/type): append only.
    enum Type : int
    {
        CHORUS = 0,
        CE5 = 1,
    };
    static constexpr int TYPE_COUNT = 2;

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
    void set_mix_pct(float pct) {
        mix_pct.store(pct);
    }
    enum class Mode : int
    {
        NORMAL = 0,
        TRI,
        VINTAGE,
        SPATIAL
    };
    void set_mode(int m) { // the registry's integer; out of range = Normal
        mode.store(m >= 0 && m <= std::to_underlying(Mode::SPATIAL) ? static_cast<Mode>(m)
                                                                    : Mode::NORMAL);
    }

    void set_type(int new_type) {
        type.store(std::clamp(new_type, 0, TYPE_COUNT - 1));
    }
    int get_type() const {
        return type.load();
    }
    // The CE-5's filter knobs, 0–100 % of the rotation (100 = fully clockwise, open).
    void set_ce5_low_pct(float pct) {
        ce5_low_pct.store(pct);
    }
    void set_ce5_high_pct(float pct) {
        ce5_high_pct.store(pct);
    }

    float get_rate_hz() const {
        return rate_hz.load();
    }
    float get_depth_pct() const {
        return depth_pct.load();
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
    atomic<float> rate_hz{1.0f};
    atomic<float> depth_pct{50.0f};
    atomic<float> mix_pct{50.0f};

    float lfo_phase{0.0f};
    atomic<Mode> mode{Mode::NORMAL};
    float vint_lp[2]{}; // Vintage wet low-pass per channel

    // Wet sample for one channel at an LFO phase (all modes).
    float wet(const DelayLine& line, float phase, float depth, float fs, Mode current_mode);
    // One modulated read of the line at an LFO phase.
    static float tap(const DelayLine& line, float phase, float depth, float fs, Mode current_mode);

    DelayLine line_l, line_r;

    // CE-5 type.
    atomic<int> type{CHORUS};
    atomic<float> ce5_low_pct{100.0f};
    atomic<float> ce5_high_pct{100.0f};
    int last_type = CHORUS; // audio thread
    ce5::Tone ce5_tone;
    ce5::ToneState ce5_state[2];
    struct Ce5Block
    {
        ce5::Sweep sweep;
        float level, input_ceiling, wet_ceiling, dry_gain, ph_inc, fs;
    };
    Ce5Block ce5_block();
    // One channel's sample: the dry (after the input clip) and the wet.
    struct Ce5Out
    {
        float dry, wet;
    };
    Ce5Out ce5_sample(DelayLine& line, ce5::ToneState& state, float x, float phase,
                      const Ce5Block& b);
    void ce5_advance(const Ce5Block& b);
    void ce5_mono(float* buf, size_t nframes);
    void ce5_dual(float* l, float* r, size_t nframes);
    void ce5_stereo_out(float* l, float* r, size_t nframes);
    void type_edge(int current_type);

    static constexpr float BASE_MS = 20.0f;
    static constexpr float MAX_DEPTH_MS = 5.0f;
};

} // namespace namx
