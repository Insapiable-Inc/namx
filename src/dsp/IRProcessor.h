#pragma once

#include <array>
#include <atomic>
#include <string>
#include <vector>

#include "EffectBase.h"
#include "IRLoader.h"

/// Cabinet: two IR slots (A, B), each with level, pan and phase, a Blend
/// between them, then Low/High Cut (12 dB/oct) on the result.
///
/// Slot A alone with default settings is exactly the old single-IR cab
/// (mono stays mono). Different pans — or a stereo input — give a stereo
/// output. Each loader counts every block (RtSlot reclaim) even when its
/// slot is empty or the cab is bypassed.
class IRProcessor : public EffectBase
{
public:
    IRProcessor();

    // Slot A (the hero row / library / presets' irPath).
    bool load(const std::string& path) {
        return mA.load(path);
    }
    void unload() {
        mA.unload();
    }
    bool isLoaded() const {
        return mA.isLoaded();
    }
    std::string filePath() const {
        return mA.getFilePath();
    }
    std::string lastError() const {
        return mA.lastError();
    }

    // Slot B.
    bool loadB(const std::string& path) {
        return mB.load(path);
    }
    void unloadB() {
        mB.unload();
    }
    bool isLoadedB() const {
        return mB.isLoaded();
    }
    std::string filePathB() const {
        return mB.getFilePath();
    }
    std::string lastErrorB() const {
        return mB.lastError();
    }

    void reclaimRetired(bool audioStopped) {
        mA.reclaimRetired(audioStopped);
        mB.reclaimRetired(audioStopped);
    }

    void setLevel(int slot, float dB) {
        mLevelDb[slot & 1].store(dB);
    }
    void setPan(int slot, float pct) {
        mPan[slot & 1].store(pct);
    } // −100 … 100
    void setPhase(int slot, bool inv) {
        mInvert[slot & 1].store(inv);
    }
    void setBlend(float pct) {
        mBlend.store(pct);
    } // 0 = A … 100 = B
    void setLowCut(float hz) {
        mLowCutHz.store(hz);
    }
    void setHighCut(float hz) {
        mHighCutHz.store(hz);
    }

    void reset() override;
    bool process(float* L, float* R, size_t n, bool inputIsStereo) override;

protected:
    void onSampleRateChanged(double sr) override;

private:
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        float process(float x) {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void clear() {
            z1 = z2 = 0.0f;
        }
    };
    void designCuts(float lowHz, float highHz);
    bool processChunk(float* L, float* R, size_t n, bool inputIsStereo);

    IRLoader mA, mB;

    std::atomic<float> mLevelDb[2]{{0.0f}, {0.0f}};
    std::atomic<float> mPan[2]{{0.0f}, {0.0f}};
    std::atomic<bool> mInvert[2]{{false}, {false}};
    std::atomic<float> mBlend{50.0f};
    std::atomic<float> mLowCutHz{20.0f};
    std::atomic<float> mHighCutHz{20000.0f};

    static constexpr size_t kChunk = 2048;
    std::vector<float> mAL, mAR, mBL, mBR; // per-slot scratch (kChunk)
    Biquad mHp[2], mLp[2];
    float mDesignedLow = -1.0f, mDesignedHigh = -1.0f;
};
