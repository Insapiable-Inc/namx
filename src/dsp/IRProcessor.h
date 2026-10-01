#pragma once

#include <array>
#include <atomic>
#include <string>
#include <vector>

#include "Common.h"
#include "EffectBase.h"
#include "IRLoader.h"

namespace npl
{

/// Cabinet: two IR slots (A, B), each with level, pan and phase, a Blend
/// between them, then Low/High Cut (12 dB/oct) on the result.
///
/// Slot A alone with default settings is exactly the old single-IR cab
/// (mono stays mono). Different pans — or a stereo input — give a stereo
/// output. Each loader counts every block (RtSlot reclaim) even when its
/// slot is empty or the cab is bypassed.
class IRProcessor : public EffectBase
{
public:
    IRProcessor();

    // Slot A (the hero row / library / presets' irPath).
    bool load(const string& path) {
        return slot_a.load(path);
    }
    void unload() {
        slot_a.unload();
    }
    bool is_loaded() const {
        return slot_a.is_loaded();
    }
    string file_path() const {
        return slot_a.get_file_path();
    }
    string last_error() const {
        return slot_a.get_last_error();
    }

    // Slot B.
    bool load_b(const string& path) {
        return slot_b.load(path);
    }
    void unload_b() {
        slot_b.unload();
    }
    bool is_loaded_b() const {
        return slot_b.is_loaded();
    }
    string file_path_b() const {
        return slot_b.get_file_path();
    }
    string last_error_b() const {
        return slot_b.get_last_error();
    }

    void reclaim_retired(bool audio_stopped) {
        slot_a.reclaim_retired(audio_stopped);
        slot_b.reclaim_retired(audio_stopped);
    }

    void set_level(int slot, float db) {
        level_db[slot & 1].store(db);
    }
    void set_pan(int slot, float pct) {
        pan_pct[slot & 1].store(pct);
    } // −100 … 100
    void set_phase(int slot, bool inv) {
        invert[slot & 1].store(inv);
    }
    void set_blend_pct(float pct) {
        blend_pct.store(pct);
    } // 0 = A … 100 = B
    void set_low_cut(float hz) {
        low_cut_hz.store(hz);
    }
    void set_high_cut(float hz) {
        high_cut_hz.store(hz);
    }

    void reset() override;
    bool process(float* l, float* r, size_t n, bool input_is_stereo) override;

protected:
    void on_sample_rate_changed(double sr) override;

private:
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        float process(float x) {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void clear() {
            z1 = z2 = 0.0f;
        }
    };
    void design_cuts(float low_hz, float high_hz);
    bool process_chunk(float* l, float* r, size_t n, bool input_is_stereo);

    IRLoader slot_a, slot_b;

    atomic<float> level_db[2]{{0.0f}, {0.0f}};
    atomic<float> pan_pct[2]{{0.0f}, {0.0f}};
    atomic<bool> invert[2]{{false}, {false}};
    atomic<float> blend_pct{50.0f};
    atomic<float> low_cut_hz{20.0f};
    atomic<float> high_cut_hz{20000.0f};

    static constexpr size_t CHUNK = 2048;
    vector<float> al, ar, bl, br; // per-slot scratch (CHUNK)
    Biquad hp[2], lp[2];
    float designed_low = -1.0f, designed_high = -1.0f;
};

} // namespace npl
