#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "NAM/dsp.h"
#include "EffectBase.h"

/// Wraps the NAM DSP model as an EffectBase.
/// Thread-safe model swapping via double-buffering (same pattern as the old AudioEngine).
class NamProcessor : public EffectBase
{
public:
    NamProcessor();
    ~NamProcessor() override;

    bool        loadModel(const std::string& path);
    void        unloadModel();
    bool        isModelLoaded() const { return mModelLoaded.load(); }
    std::string modelPath()     const { return mModelPath; }

    /// Must be called whenever the backend reports a (new) buffer size, before
    /// the next process callback. Re-Reset()s loaded models if the size grew.
    void prepareBuffer(int maxBuf);
    int  maxBuf() const { return mMaxBuf.load(); }

    void reset() override {}

    /// Overrides the default EffectBase dispatch so that stereo input is
    /// processed through independent L and R model instances rather than
    /// calling the single-state processBlockMono twice per block.
    bool process(float* L, float* R, int n, bool inputIsStereo) override;

protected:
    void onSampleRateChanged(double sr) override;
    void processBlockMono(float* buf, int n) override;  // uses L-channel model
    void processBlockMonoR(float* buf, int n);          // uses R-channel model

private:
    // Double-buffered model pairs: AL/AR are slot 0, BL/BR are slot 1.
    // Both channels in a slot are loaded from the same file so they apply
    // identical non-linearity; their recurrent states evolve independently.
    std::unique_ptr<nam::DSP> mModelAL, mModelAR;
    std::unique_ptr<nam::DSP> mModelBL, mModelBR;
    std::atomic<int>          mActiveModel{0};
    std::mutex                mModelMutex;
    std::atomic<bool>         mModelLoaded{false};
    std::string               mModelPath;
    std::atomic<int>          mMaxBuf{0};
    int                       mModelResetBuf{0}; // maxBuf the live models were Reset() with (guarded by mModelMutex)
    std::vector<float>        mScratchL, mScratchR;

    // Click-free swap: load/unload flag a short output fade-in, applied by
    // the audio thread, that masks the waveform discontinuity.
    std::atomic<bool>         mFadePending{false};
    int                       mFadeTotal{1};     // audio thread only
    int                       mFadeRemaining{0}; // audio thread only

    nam::DSP* activeModelL() const
    {
        return mActiveModel.load() == 0 ? mModelAL.get() : mModelBL.get();
    }
    nam::DSP* activeModelR() const
    {
        return mActiveModel.load() == 0 ? mModelAR.get() : mModelBR.get();
    }
};
