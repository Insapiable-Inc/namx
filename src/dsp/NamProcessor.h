#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <NAM/dsp.h>

#include "Common.h"
#include "EffectBase.h"
#include "RtSlot.h"

namespace namx
{

class ModelResampler; // ModelResampler.h — not included here, see Resampler below

// Wraps the NAM DSP model as an EffectBase.
//
// Model swaps go through an RtSlot: the GUI thread publishes a freshly
// loaded L/R pair, the audio thread binds one pair per block, and the
// previous pair is destroyed only after the audio thread has provably
// finished with it. Loads never block and can arrive at any rate.
class NamProcessor : public EffectBase
{
public:
    NamProcessor();
    ~NamProcessor() override;

    bool load_model(const string& path);
    void unload_model();
    bool is_model_loaded() const {
        return slot.current() != nullptr;
    }
    string get_model_path() const {
        return model_path;
    }

    // While the model is resampled (engine rate ≠ the model's training rate): the
    // model's rate; 0 when it runs directly or nothing is loaded. Any thread.
    double get_resampled_model_rate() const {
        return resampled_model_rate.load(std::memory_order_relaxed);
    }

    // The resampler's delay while resampling (see get_resampled_model_rate).
    double latency_ms() const override {
        return enabled.load() ? resample_latency_ms.load(std::memory_order_relaxed) : 0.0;
    }

    // Loudness normalization (on by default, like the official plugin's "Normalized"
    // output mode): models that state their loudness are brought to -18 dB, so
    // switching models doesn't jump in level. Models without it play as they are.
    void set_normalize(bool on) {
        normalize.store(on, std::memory_order_relaxed);
    }
    bool get_normalize() const {
        return normalize.load(std::memory_order_relaxed);
    }
    // The user's output trim on top of the normalization (clean models can sit quiet).
    void set_gain_db(float db) {
        gain_db.store(db, std::memory_order_relaxed);
    }
    // The gain normalization applies to the loaded model, in dB; empty when it's off
    // or the model doesn't state its loudness (GUI thread only).
    std::optional<double> get_normalization_db() const {
        return get_normalize() ? model_normalization_db : std::nullopt;
    }

    // Human-readable reason the last loadModel() returned false (GUI thread only).
    string get_last_error() const {
        return last_error;
    }

    // Free retired model pairs. Called by the engine whenever the audio
    // thread is known to be stopped (so pairs retired while it was idle
    // don't linger); otherwise loads reclaim lazily.
    void reclaim_retired(bool audio_stopped) {
        slot.reclaim(audio_stopped);
    }

    // Must be called whenever the backend reports a (new) buffer size, before
    // the next process callback. Re-Reset()s the live models if the size grew.
    void prepare_buffer(size_t new_max_buf);
    size_t get_max_buf() const {
        return max_buf.load();
    }

    void reset() override {
    }

    // Overrides the default EffectBase dispatch so that stereo input is
    // processed through independent L and R model instances rather than
    // calling the single-state process_block_mono twice per block.
    bool process(float* l, float* r, size_t n, bool input_is_stereo) override;

protected:
    void on_sample_rate_changed(double sr) override;

private:
    // Runs a model at its training rate inside an engine running at another one
    // (Lanczos, A = 12 — the same resampler the official NAM plugin uses).
    // Defined in ModelResampler.h, included only by NamProcessor.cpp (it brings
    // <windows.h> on Windows). ModelPair's constructor/destructor are out of line for it.
    using Resampler = ModelResampler;

    // One published unit: both channels loaded from the same file so they
    // apply identical non-linearity; their recurrent states evolve
    // independently. reset_buf/reset_rate record what reset() was last called
    // with (written only while no audio callback is in flight).
    struct ModelPair
    {
        unique_ptr<nam::DSP> l, r;
        // Present only when the engine rate differs from the model's training rate.
        unique_ptr<Resampler> resampler_l, resampler_r;
        size_t reset_buf{0};
        double reset_rate{0.0};
        // Linear gain to the normalization target (1 when the model's loudness is unknown).
        float loudness_gain{1.0f};

        ModelPair();
        ~ModelPair();

        // The rate the model was trained at; models that don't say are 48 kHz (as
        // in the official plugin).
        double native_rate() const {
            const double expected = l->GetExpectedSampleRate();
            return expected > 0.0 ? expected : 48000.0;
        }

        // Prepares both models for engine blocks of up to `max_frames` at engine rate
        // `rate`: directly when the rates match, otherwise at the model's own rate
        // behind a resampler (code review item 15). Allocates — GUI thread, or with
        // processing suspended.
        void reset(double rate, size_t max_frames);
    };

    RtSlot<ModelPair> slot;
    std::mutex model_mutex; // serializes publish vs. prepareBuffer / SR change
    string model_path; // GUI thread only
    string last_error; // GUI thread only
    std::optional<double> model_normalization_db; // GUI thread only
    atomic<size_t> max_buf{0};
    atomic<double> resampled_model_rate{0.0}; // see get_resampled_model_rate()
    atomic<double> resample_latency_ms{0.0}; // see latency_ms()
    void publish_resampling(const ModelPair* p); // updates the two atomics above
    atomic<bool> normalize{true};
    atomic<float> gain_db{0.0f};
    // Audio thread only: the output gain as it glides to its target, and the pair it
    // last applied to (a new pair takes its gain at once; the swap fade-in hides it).
    float output_gain{1.0f};
    const ModelPair* gain_pair{nullptr};
    double model_rate{0.0}; // engine SR as last propagated (guarded by model_mutex)
    vector<float> scratch_l, scratch_r;

    // Click-free swap: load/unload flag a short output fade-in, applied by
    // the audio thread, that masks the waveform discontinuity.
    atomic<bool> fade_pending{false};
    size_t fade_total{1}; // samples; audio thread only
    size_t fade_remaining{0}; // audio thread only

    void apply_output_gain(const ModelPair* p, float* l, float* r, size_t n);
    static void run_model(nam::DSP* model, Resampler* resampler, float* buf, size_t n,
                          vector<float>& scratch);
};

} // namespace namx
