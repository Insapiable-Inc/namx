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

    bool loadModel(const std::string& path);
    void unloadModel();
    bool isModelLoaded() const {
        return mSlot.current() != nullptr;
    }
    std::string modelPath() const {
        return mModelPath;
    }

    /// Human-readable reason the last loadModel() returned false (GUI thread only).
    std::string lastError() const {
        return mLastError;
    }

    /// Free retired model pairs. Called by the engine whenever the audio
    /// thread is known to be stopped (so pairs retired while it was idle
    /// don't linger); otherwise loads reclaim lazily.
    void reclaimRetired(bool audioStopped) {
        mSlot.reclaim(audioStopped);
    }

    /// Must be called whenever the backend reports a (new) buffer size, before
    /// the next process callback. Re-Reset()s the live models if the size grew.
    void prepareBuffer(size_t maxBuf);
    size_t maxBuf() const {
        return mMaxBuf.load();
    }

    void reset() override {
    }

    /// Stereo input → run the model per channel (on) or sum to mono and run
    /// it once (off, the default: half the CPU, and an amp is mono anyway).
    void setStereoAmp(bool on) {
        mStereoAmp.store(on);
    }

    /// Overrides the default EffectBase dispatch so that stereo input is
    /// processed through independent L and R model instances rather than
    /// calling the single-state processBlockMono twice per block.
    bool process(float* L, float* R, size_t n, bool inputIsStereo) override;

protected:
    void onSampleRateChanged(double sr) override;

private:
    // One published unit: both channels loaded from the same file so they
    // apply identical non-linearity; their recurrent states evolve
    // independently. resetBuf/resetRate record what Reset() was last called
    // with (written only while no audio callback is in flight).
    struct ModelPair
    {
        std::unique_ptr<nam::DSP> L, R;
        size_t resetBuf{0};
        double resetRate{0.0};

        // Reset() + prewarm() both models for blocks of up to `maxFrames`, and record it.
        void reset(double rate, size_t maxFrames) {
            const int frames = static_cast<int>(maxFrames); // the NAM core counts in int
            L->Reset(rate, frames);
            L->prewarm();
            R->Reset(rate, frames);
            R->prewarm();
            resetBuf = maxFrames;
            resetRate = rate;
        }
    };

    RtSlot<ModelPair> mSlot;
    std::mutex mModelMutex; // serializes publish vs. prepareBuffer / SR change
    std::string mModelPath; // GUI thread only
    std::string mLastError; // GUI thread only
    std::atomic<size_t> mMaxBuf{0};
    std::atomic<bool> mStereoAmp{false};
    double mModelRate{0.0}; // engine SR as last propagated (guarded by mModelMutex)
    std::vector<float> mScratchL, mScratchR;

    // Click-free swap: load/unload flag a short output fade-in, applied by
    // the audio thread, that masks the waveform discontinuity.
    std::atomic<bool> mFadePending{false};
    size_t mFadeTotal{1}; // samples; audio thread only
    size_t mFadeRemaining{0}; // audio thread only

    static void runModel(nam::DSP* model, float* buf, size_t n, std::vector<float>& scratch);
};
