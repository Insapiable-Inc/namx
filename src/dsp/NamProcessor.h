#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <NAM/dsp.h>

#include "Common.h"
#include "EffectBase.h"
#include "ModelResampler.h"
#include "RtSlot.h"

namespace npl
{

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

    // Stereo input → run the model per channel (on) or sum to mono and run
    // it once (off, the default: half the CPU, and an amp is mono anyway).
    void set_stereo_amp(bool on) {
        stereo_amp.store(on);
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
    using Resampler = dsp::ResamplingContainer<float, 1, 12>;

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
    atomic<size_t> max_buf{0};
    atomic<double> resampled_model_rate{0.0}; // see get_resampled_model_rate()
    atomic<bool> stereo_amp{false};
    double model_rate{0.0}; // engine SR as last propagated (guarded by model_mutex)
    vector<float> scratch_l, scratch_r;

    // Click-free swap: load/unload flag a short output fade-in, applied by
    // the audio thread, that masks the waveform discontinuity.
    atomic<bool> fade_pending{false};
    size_t fade_total{1}; // samples; audio thread only
    size_t fade_remaining{0}; // audio thread only

    static void run_model(nam::DSP* model, Resampler* resampler, float* buf, size_t n,
                          vector<float>& scratch);
};

} // namespace npl
