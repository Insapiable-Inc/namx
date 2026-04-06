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

    /// Must be called after JACK reports its buffer size so Reset() gets the right value.
    void prepareBuffer(int maxBuf);

    void reset() override {}

protected:
    void onSampleRateChanged(double sr) override;
    void processBlockMono(float* buf, int n) override;

private:
    std::unique_ptr<nam::DSP> mModelA, mModelB;
    std::atomic<int>          mActiveModel{0};
    std::mutex                mModelMutex;
    std::atomic<bool>         mModelLoaded{false};
    std::string               mModelPath;
    int                       mMaxBuf{0};
    std::vector<float>        mScratch;

    nam::DSP* activeModel() const
    {
        return mActiveModel.load() == 0 ? mModelA.get() : mModelB.get();
    }
};
