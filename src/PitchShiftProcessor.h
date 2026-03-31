#pragma once

#include <atomic>
#include <vector>
#include "EffectBase.h"

/// Granular pitch shifter using two overlapping Hann-windowed grains.
/// Semitones: -12 (one octave down) to +12 (one octave up), integer steps.
/// Mix: 0 = dry, 100 = fully wet.
class PitchShiftProcessor : public EffectBase
{
public:
    void setSampleRate(double sr) override;
    void reset() override;
    void processBlock(float* buf, int nframes) override;

    // Semitones: -12.0 to +12.0 (DSP rounds to nearest integer)
    void setSemitones(float st)  { mSemitones.store(st); }
    void setMix(float pct)       { mMix.store(pct); }

    float semitones() const { return mSemitones.load(); }
    float mix()       const { return mMix.load(); }

private:
    std::atomic<float> mSemitones{0.0f};
    std::atomic<float> mMix      {100.0f};

    double mSampleRate{44100.0};

    // Circular input buffer
    std::vector<float> mBuf;
    int                mWritePos{0};

    // Two grain read pointers (float for sub-sample accuracy) and phases
    float mReadPosA{0.f}, mReadPosB{0.f};
    int   mPhaseA{0},      mPhaseB{0};

    static constexpr int kBufSize   = 8192;
    static constexpr int kGrainSize = 2048; // ~46 ms @ 44100 Hz

    // Linear interpolation from circular buffer
    float readInterp(float pos) const;
};
