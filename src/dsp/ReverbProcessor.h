#pragma once

#include <array>
#include <atomic>
#include <vector>

#include "EffectBase.h"
#include "PitchShifter.h"

/// Freeverb-based stereo reverb (8 comb + 4 all-pass filters).
class ReverbProcessor : public EffectBase
{
public:
    ReverbProcessor();

    bool supportsStereoOut() const override {
        return true;
    }
    void reset() override;

    void setRoomSize(float pct) {
        mRoomSize.store(pct);
    }
    void setDamping(float pct) {
        mDamping.store(pct);
    }
    void setWidth(float pct) {
        mWidth.store(pct);
    }
    void setMix(float pct) {
        mMix.store(pct);
    }
    void setPreDelay(float ms) {
        mPreDelayMs.store(ms);
    } // 0–250 ms before the tail
    void setLowCut(float hz) {
        mLowCutHz.store(hz);
    } // on the wet signal
    void setHighCut(float hz) {
        mHighCutHz.store(hz);
    }
    /// Octave-up feedback into the tail (0–100 %): the shimmer reverb.
    void setShimmer(float pct) {
        mShimmer.store(pct);
    }

    float roomSize() const {
        return mRoomSize.load();
    }
    float damping() const {
        return mDamping.load();
    }
    float width() const {
        return mWidth.load();
    }
    float mix() const {
        return mMix.load();
    }

protected:
    void onSampleRateChanged(double sr) override;
    void processBlockMono(float* buf, int nframes) override;
    void processBlockDual(float* L, float* R, int nframes) override;
    void processBlockStereoOut(float* L, float* R, int nframes) override;

private:
    std::atomic<float> mRoomSize{50.0f};
    std::atomic<float> mDamping{50.0f};
    std::atomic<float> mWidth{100.0f};
    std::atomic<float> mMix{30.0f};
    std::atomic<float> mPreDelayMs{0.0f};
    std::atomic<float> mLowCutHz{20.0f};
    std::atomic<float> mHighCutHz{20000.0f};
    std::atomic<float> mShimmer{0.0f};
    PitchShifter mShift[2]; // per bank, +12 st
    float mShimFb[2]{}; // shifted wet fed into the next input sample

    static constexpr float kMaxPreDelayMs = 250.0f;
    // Pre-delay line per input bank ([0] mono/L/sum, [1] R) + wet filters per output.
    std::vector<float> mPre[2];
    int mPreWrite[2]{};
    float mHpX[2]{}, mHpY[2]{}, mLp[2]{};

    float preDelay(int bank, float x, int delaySamples);
    float wetFilter(int ch, float x, float hpA, float lpK);

    struct CombFilter
    {
        std::vector<float> buf;
        int writePos{0};
        float filterStore{0.0f};

        void resize(int n) {
            buf.assign(n, 0.0f);
        }
        void reset() {
            std::fill(buf.begin(), buf.end(), 0.0f);
            filterStore = 0.0f;
            writePos = 0;
        }

        float process(float in, float feedback, float damp) {
            int sz = static_cast<int>(buf.size());
            float out = buf[writePos];
            filterStore = out * (1.0f - damp) + filterStore * damp;
            buf[writePos] = in + filterStore * feedback;
            if (++writePos == sz) {
                writePos = 0;
            }
            return out;
        }
    };

    struct AllPassFilter
    {
        std::vector<float> buf;
        int writePos{0};
        static constexpr float kFeedback = 0.5f;

        void resize(int n) {
            buf.assign(n, 0.0f);
        }
        void reset() {
            std::fill(buf.begin(), buf.end(), 0.0f);
            writePos = 0;
        }

        float process(float in) {
            int sz = static_cast<int>(buf.size());
            float bufOut = buf[writePos];
            float output = -in + bufOut;
            buf[writePos] = in + bufOut * kFeedback;
            if (++writePos == sz) {
                writePos = 0;
            }
            return output;
        }
    };

    static constexpr int kNumCombs = 8;
    static constexpr int kNumAllPasses = 4;

    std::array<CombFilter, kNumCombs> mCombL, mCombR;
    std::array<AllPassFilter, kNumAllPasses> mApL, mApR;

    static constexpr int kCombLengths[kNumCombs] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static constexpr int kApLengths[kNumAllPasses] = {556, 441, 341, 225};
    static constexpr int kStereoOffset = 23;

    void allocate();
    void processMonoBank(float* buf, int nframes, int bank,
                         std::array<CombFilter, kNumCombs>& combs,
                         std::array<AllPassFilter, kNumAllPasses>& aps);
};
