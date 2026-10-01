#pragma once

#include <array>
#include <atomic>

#include "DelayLine.h"
#include "EffectBase.h"
#include "PitchShifter.h"

/// Freeverb-based stereo reverb (8 comb + 4 all-pass filters).
class ReverbProcessor : public EffectBase
{
public:
    ReverbProcessor();

    bool supports_stereo_out() const override {
        return true;
    }
    void reset() override;

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
    } // 0–250 ms before the tail
    void set_low_cut(float hz) {
        low_cut_hz.store(hz);
    } // on the wet signal
    void set_high_cut(float hz) {
        high_cut_hz.store(hz);
    }
    /// Octave-up feedback into the tail (0–100 %): the shimmer reverb.
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
    std::atomic<float> room_size_pct{50.0f};
    std::atomic<float> damping_pct{50.0f};
    std::atomic<float> width_pct{100.0f};
    std::atomic<float> mix_pct{30.0f};
    std::atomic<float> pre_delay_ms{0.0f};
    std::atomic<float> low_cut_hz{20.0f};
    std::atomic<float> high_cut_hz{20000.0f};
    std::atomic<float> shimmer_pct{0.0f};
    PitchShifter shift[2]; // per bank, +12 st
    float shim_fb[2]{}; // shifted wet fed into the next input sample

    static constexpr float MAX_PRE_DELAY_MS = 250.0f;
    // Pre-delay line per input bank ([0] mono/L/sum, [1] R) + wet filters per output.
    DelayLine pre[2];
    float hp_x[2]{}, hp_y[2]{}, lp[2]{};

    float pre_delay(int bank, float x, size_t delay_samples);
    float wet_filter(int ch, float x, float hp_a, float lp_k);

    struct CombFilter
    {
        DelayLine line;
        float filter_store{0.0f};

        void resize(size_t n) {
            line.resize(n);
        }
        void reset() {
            line.clear();
            filter_store = 0.0f;
        }

        float process(float in, float feedback, float damp) {
            const float out = line.oldest();
            filter_store = out * (1.0f - damp) + filter_store * damp;
            line.push(in + filter_store * feedback);
            return out;
        }
    };

    struct AllPassFilter
    {
        DelayLine line;
        static constexpr float FEEDBACK = 0.5f;

        void resize(size_t n) {
            line.resize(n);
        }
        void reset() {
            line.clear();
        }

        float process(float in) {
            const float buf_out = line.oldest();
            line.push(in + buf_out * FEEDBACK);
            return -in + buf_out;
        }
    };

    static constexpr size_t NUM_COMBS = 8;
    static constexpr size_t NUM_ALL_PASSES = 4;

    std::array<CombFilter, NUM_COMBS> comb_l, comb_r;
    std::array<AllPassFilter, NUM_ALL_PASSES> ap_l, ap_r;

    static constexpr int COMB_LENGTHS[NUM_COMBS] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static constexpr int AP_LENGTHS[NUM_ALL_PASSES] = {556, 441, 341, 225};
    static constexpr int STEREO_OFFSET = 23;

    void allocate();
    void process_mono_bank(float* buf, size_t nframes, int bank,
                           std::array<CombFilter, NUM_COMBS>& combs,
                           std::array<AllPassFilter, NUM_ALL_PASSES>& aps);
};
