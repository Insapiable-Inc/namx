#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <NAM/dsp.h>

#include "EffectBase.h"
#include "RtSlot.h"

/// Wraps the NAM DSP model as an EffectBase.
///
/// Model swaps go through an RtSlot: the GUI thread publishes a freshly
/// loaded L/R pair, the audio thread binds one pair per block, and the
/// previous pair is destroyed only after the audio thread has provably
/// finished with it. Loads never block and can arrive at any rate.
class NamProcessor : public EffectBase
{
public:
    NamProcessor();
    ~NamProcessor() override;

    bool load_model(const std::string& path);
    void unload_model();
    bool is_model_loaded() const {
        return m_slot.current() != nullptr;
    }
    std::string get_model_path() const {
        return m_model_path;
    }

    /// Human-readable reason the last loadModel() returned false (GUI thread only).
    std::string get_last_error() const {
        return m_last_error;
    }

    /// Free retired model pairs. Called by the engine whenever the audio
    /// thread is known to be stopped (so pairs retired while it was idle
    /// don't linger); otherwise loads reclaim lazily.
    void reclaim_retired(bool audio_stopped) {
        m_slot.reclaim(audio_stopped);
    }

    /// Must be called whenever the backend reports a (new) buffer size, before
    /// the next process callback. Re-Reset()s the live models if the size grew.
    void prepare_buffer(size_t max_buf);
    size_t get_max_buf() const {
        return m_max_buf.load();
    }

    void reset() override {
    }

    /// Stereo input → run the model per channel (on) or sum to mono and run
    /// it once (off, the default: half the CPU, and an amp is mono anyway).
    void set_stereo_amp(bool on) {
        m_stereo_amp.store(on);
    }

    /// Overrides the default EffectBase dispatch so that stereo input is
    /// processed through independent L and R model instances rather than
    /// calling the single-state process_block_mono twice per block.
    bool process(float* l, float* r, size_t n, bool input_is_stereo) override;

protected:
    void on_sample_rate_changed(double sr) override;

private:
    // One published unit: both channels loaded from the same file so they
    // apply identical non-linearity; their recurrent states evolve
    // independently. resetBuf/resetRate record what Reset() was last called
    // with (written only while no audio callback is in flight).
    struct ModelPair
    {
        std::unique_ptr<nam::DSP> l, r;
        size_t reset_buf{0};
        double reset_rate{0.0};

        // Reset() + prewarm() both models for blocks of up to `maxFrames`, and record it.
        void reset(double rate, size_t max_frames) {
            const int frames = static_cast<int>(max_frames); // the NAM core counts in int
            l->Reset(rate, frames);
            l->prewarm();
            r->Reset(rate, frames);
            r->prewarm();
            reset_buf = max_frames;
            reset_rate = rate;
        }
    };

    RtSlot<ModelPair> m_slot;
    std::mutex m_model_mutex; // serializes publish vs. prepareBuffer / SR change
    std::string m_model_path; // GUI thread only
    std::string m_last_error; // GUI thread only
    std::atomic<size_t> m_max_buf{0};
    std::atomic<bool> m_stereo_amp{false};
    double m_model_rate{0.0}; // engine SR as last propagated (guarded by mModelMutex)
    std::vector<float> m_scratch_l, m_scratch_r;

    // Click-free swap: load/unload flag a short output fade-in, applied by
    // the audio thread, that masks the waveform discontinuity.
    std::atomic<bool> m_fade_pending{false};
    size_t m_fade_total{1}; // samples; audio thread only
    size_t m_fade_remaining{0}; // audio thread only

    static void run_model(nam::DSP* model, float* buf, size_t n, std::vector<float>& scratch);
};
