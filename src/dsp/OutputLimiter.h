#pragma once

#include <atomic>
#include <vector>

/// Stereo-linked look-ahead peak limiter on the output stage (after the
/// output gain, before the metronome): 1 ms look-ahead, ceiling −0.3 dBFS,
/// 80 ms release. When off it costs nothing and adds no latency; when on it
/// delays the output by the 1 ms look-ahead.
class OutputLimiter
{
public:
    void setSampleRate(double sr); // non-RT
    void setEnabled(bool on) {
        mEnabled.store(on);
    }
    bool isEnabled() const {
        return mEnabled.load();
    }

    /// Deepest reduction since the last take (dB ≥ 0), for a meter.
    float takeReductionDb() {
        return mGrPeakDb.exchange(0.0f);
    }

    void process(float* L, float* R, size_t n);

private:
    std::atomic<bool> mEnabled{false};
    std::atomic<float> mGrPeakDb{0.0f};
    std::vector<float> mDelayL, mDelayR, mNeed; // look-ahead lines + required gain per slot
    int mPos = 0;
    int mLook = 48;
    float mGain = 1.0f;
    float mRelease = 0.9997f;
    float mAttack = 0.2f;
    bool mWasEnabled = false;
};
