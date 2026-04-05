#pragma once

#include <atomic>
#include <array>
#include <vector>
#include "EffectBase.h"

/// Reverse playback: double-buffered chunk reversal.
///
/// Audio is written into one buffer while the previously captured chunk is
/// played back in reverse.  When the fill buffer is full the two swap, so
/// playback is always one chunk behind the live signal.  Higher mix levels
/// let more of the reversed audio through; at 100% you hear pure reversal.
class ReverseProcessor : public EffectBase
{
public:
    void setSampleRate(double sr) override;
    void reset() override;

    void processBlock(float* buf, int nframes) override;
    void processBlockStereo(float* L, float* R, int nframes) override;

    void setLength(float ms)  { mLength.store(ms); }  // 50–2000 ms
    void setMix(float pct)    { mMix.store(pct);   }  // 0–100

    float length() const { return mLength.load(); }
    float mix()    const { return mMix.load();    }

private:
    std::atomic<float> mLength{500.0f};
    std::atomic<float> mMix   { 70.0f};

    double mSampleRate{44100.0};
    int    mMaxBufSize{0};

    // Two buffer pairs: [0] and [1].  mFillIdx is the one being written.
    std::array<std::vector<float>, 2> mBufL, mBufR;
    int mFillIdx{0};
    int mPhase  {0};
};
