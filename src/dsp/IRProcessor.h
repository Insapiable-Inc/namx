#pragma once

#include <string>

#include "EffectBase.h"
#include "IRLoader.h"

/// Wraps IRLoader as an EffectBase.
/// Always applies the IR to both channels with independent convolution state,
/// so process() is overridden directly rather than using the default dispatch.
class IRProcessor : public EffectBase
{
public:
    bool load(const std::string& path) { return mLoader.load(path); }
    void unload()                      { mLoader.unload(); }
    bool isLoaded() const              { return mLoader.isLoaded(); }
    std::string filePath() const       { return mLoader.getFilePath(); }
    std::string lastError() const      { return mLoader.lastError(); }
    void reclaimRetired(bool audioStopped) { mLoader.reclaimRetired(audioStopped); }

    void reset() override { mLoader.reset(); }

    /// IR never changes whether the signal is stereo; it filters what it gets
    /// (one convolution for mono input, one per channel for stereo). The
    /// loader is always called so it can count the block even when bypassed.
    bool process(float* L, float* R, int n, bool inputIsStereo) override
    {
        mLoader.processBlock(L, R, n, inputIsStereo, mEnabled.load());
        return inputIsStereo;
    }

protected:
    void onSampleRateChanged(double sr) override { mLoader.setTargetSampleRate(sr); }

private:
    IRLoader mLoader;
};
