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

    void set_room_size(float pct) {
        m_room_size.store(pct);
    }
    void set_damping(float pct) {
        m_damping.store(pct);
    }
    void set_width(float pct) {
        m_width.store(pct);
    }
    void set_mix(float pct) {
        m_mix.store(pct);
    }
    void set_pre_delay(float ms) {
        m_pre_delay_ms.store(ms);
    } // 0–250 ms before the tail
    void set_low_cut(float hz) {
        m_low_cut_hz.store(hz);
    } // on the wet signal
    void set_high_cut(float hz) {
        m_high_cut_hz.store(hz);
    }
    /// Octave-up feedback into the tail (0–100 %): the shimmer reverb.
    void set_shimmer(float pct) {
        m_shimmer.store(pct);
    }

    float get_room_size() const {
        return m_room_size.load();
    }
    float get_damping() const {
        return m_damping.load();
    }
    float get_width() const {
        return m_width.load();
    }
    float get_mix() const {
        return m_mix.load();
    }

protected:
    void on_sample_rate_changed(double sr) override;
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;
    void process_block_stereo_out(float* l, float* r, size_t nframes) override;

private:
    std::atomic<float> m_room_size{50.0f};
    std::atomic<float> m_damping{50.0f};
    std::atomic<float> m_width{100.0f};
    std::atomic<float> m_mix{30.0f};
    std::atomic<float> m_pre_delay_ms{0.0f};
    std::atomic<float> m_low_cut_hz{20.0f};
    std::atomic<float> m_high_cut_hz{20000.0f};
    std::atomic<float> m_shimmer{0.0f};
    PitchShifter m_shift[2]; // per bank, +12 st
    float m_shim_fb[2]{}; // shifted wet fed into the next input sample

    static constexpr float K_MAX_PRE_DELAY_MS = 250.0f;
    // Pre-delay line per input bank ([0] mono/L/sum, [1] R) + wet filters per output.
    DelayLine m_pre[2];
    float m_hp_x[2]{}, m_hp_y[2]{}, m_lp[2]{};

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
        static constexpr float K_FEEDBACK = 0.5f;

        void resize(size_t n) {
            line.resize(n);
        }
        void reset() {
            line.clear();
        }

        float process(float in) {
            const float buf_out = line.oldest();
            line.push(in + buf_out * K_FEEDBACK);
            return -in + buf_out;
        }
    };

    static constexpr size_t K_NUM_COMBS = 8;
    static constexpr size_t K_NUM_ALL_PASSES = 4;

    std::array<CombFilter, K_NUM_COMBS> m_comb_l, m_comb_r;
    std::array<AllPassFilter, K_NUM_ALL_PASSES> m_ap_l, m_ap_r;

    static constexpr int K_COMB_LENGTHS[K_NUM_COMBS] = {1116, 1188, 1277, 1356,
                                                        1422, 1491, 1557, 1617};
    static constexpr int K_AP_LENGTHS[K_NUM_ALL_PASSES] = {556, 441, 341, 225};
    static constexpr int K_STEREO_OFFSET = 23;

    void allocate();
    void process_mono_bank(float* buf, size_t nframes, int bank,
                           std::array<CombFilter, K_NUM_COMBS>& combs,
                           std::array<AllPassFilter, K_NUM_ALL_PASSES>& aps);
};
