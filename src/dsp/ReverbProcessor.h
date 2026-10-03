#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "Common.h"
#include "DelayLine.h"
#include "EffectBase.h"
#include "PitchShifter.h"
#include "ReverbAlgorithms.h"

namespace namx
{

// Stereo reverb with three tanks (ReverbAlgorithms.h): Plate (Dattorro), Hall and Room
// (feedback delay networks). Around them: pre-delay, Low/High Cut on the wet signal,
// Width, Mix, and the shimmer feedback (octave-up wet back into the input).
// Phase 22, 2026-10-03: replaced Freeverb (metallic, and its Decay squeezed 0.2–0.8 s
// into three quarters of the knob, then 9 s in the last).
class ReverbProcessor : public EffectBase
{
public:
    enum class Type
    {
        PLATE,
        HALL,
        ROOM
    };

    ReverbProcessor();

    bool supports_stereo_out() const override {
        return true;
    }
    void reset() override;

    // Decay knob position (0–100 %; the preset key keeps its old name, roomSize) → RT60
    // in seconds, 0.2 s … 10 s on a log scale. Measured within ~10 % in the mids for
    // every type and damping (BENCHMARKS.md).
    static float decay_seconds(float pct) {
        return 0.2f * std::pow(50.0f, std::clamp(pct, 0.0f, 100.0f) / 100.0f);
    }

    void set_type(int t) {
        type.store(std::clamp(t, 0, 2));
    }
    void set_room_size_pct(float pct) {
        room_size_pct.store(pct);
    }
    void set_damping_pct(float pct) {
        damping_pct.store(pct);
    }
    void set_width_pct(float pct) {
        width_pct.store(pct);
    }
    void set_mix_pct(float pct) {
        mix_pct.store(pct);
    }
    void set_pre_delay(float ms) {
        pre_delay_ms.store(ms);
    } // before the tail: 0–250 ms on the knob, up to 2 s synced
    void set_low_cut(float hz) {
        low_cut_hz.store(hz);
    } // on the wet signal
    void set_high_cut(float hz) {
        high_cut_hz.store(hz);
    }
    // Octave-up feedback into the tail (0–100 %): the shimmer reverb.
    void set_shimmer_pct(float pct) {
        shimmer_pct.store(pct);
    }

    float get_room_size_pct() const {
        return room_size_pct.load();
    }
    float get_damping_pct() const {
        return damping_pct.load();
    }
    float get_width_pct() const {
        return width_pct.load();
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
    atomic<int> type{static_cast<int>(Type::PLATE)};
    atomic<float> room_size_pct{50.0f};
    atomic<float> damping_pct{50.0f};
    atomic<float> width_pct{100.0f};
    atomic<float> mix_pct{30.0f};
    atomic<float> pre_delay_ms{0.0f};
    atomic<float> low_cut_hz{20.0f};
    atomic<float> high_cut_hz{20000.0f};
    atomic<float> shimmer_pct{0.0f};
    PitchShifter shift; // +12 st, for the shimmer
    float shim_fb = 0.0f; // shifted wet fed into the next input sample

    static constexpr float MAX_PRE_DELAY_MS = 2000.0f; // a synced note value can be long
    // Pre-delay line per input channel + wet filters per output channel.
    DelayLine pre[2];
    float hp_x[2]{}, hp_y[2]{}, lp[2]{};

    reverb_tanks::Plate plate;
    reverb_tanks::Fdn hall, room;
    int active_type = 0; // audio thread: the tank that ran last
    // Level match between the tanks (and to the old Freeverb) at equal decay: wet energy
    // of a unit impulse at 0.83 s.
    static constexpr float PLATE_TRIM = 0.616f, HALL_TRIM = 2.21f, ROOM_TRIM = 1.148f;

    float pre_delay(int ch, float x, size_t delay_samples);
    float wet_filter(int ch, float x, float hp_a, float lp_k);
    void configure_tank(int t);
    void tank(int t, float in_l, float in_r, float& out_l, float& out_r);
    // Stereo in (L == R for mono), stereo wet out; mono out = the mean of the two.
    void run(float* l, float* r, size_t nframes, bool stereo_in, bool stereo_out);
};

} // namespace namx
