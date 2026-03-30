#pragma once

#include <atomic>
#include <array>
#include <vector>
#include "EffectBase.h"

/// Stereo reverb based on the Freeverb algorithm.
/// (8 parallel comb filters → 4 series all-pass filters, stereo.)
class ReverbProcessor : public EffectBase
{
public:
    ReverbProcessor();

    void setSampleRate(double sr) override;
    void reset() override;

    // Stereo is the natural mode; mono falls back to L-only
    void processBlock(float* buf, int nframes) override;
    void processBlockStereo(float* L, float* R, int nframes) override;

    void setRoomSize(float pct)  { mRoomSize.store(pct);  }  // 0–100
    void setDamping(float pct)   { mDamping.store(pct);   }  // 0–100
    void setWidth(float pct)     { mWidth.store(pct);     }  // 0–100
    void setMix(float pct)       { mMix.store(pct);       }  // 0–100

    float roomSize() const { return mRoomSize.load(); }
    float damping()  const { return mDamping.load();  }
    float width()    const { return mWidth.load();    }
    float mix()      const { return mMix.load();      }

private:
    std::atomic<float> mRoomSize{50.0f};
    std::atomic<float> mDamping {50.0f};
    std::atomic<float> mWidth   {100.0f};
    std::atomic<float> mMix     {30.0f};

    double mSampleRate{44100.0};

    // Freeverb comb filter
    struct CombFilter
    {
        std::vector<float> buf;
        int    writePos{0};
        float  filterStore{0.0f};

        void resize(int n) { buf.assign(n, 0.0f); }
        void reset() { std::fill(buf.begin(), buf.end(), 0.0f); filterStore = 0.0f; writePos = 0; }

        float process(float in, float feedback, float damp)
        {
            int sz     = static_cast<int>(buf.size());
            float out  = buf[writePos];
            filterStore = out * (1.0f - damp) + filterStore * damp;
            buf[writePos] = in + filterStore * feedback;
            writePos = (writePos + 1) % sz;
            return out;
        }
    };

    // Freeverb all-pass filter
    struct AllPassFilter
    {
        std::vector<float> buf;
        int writePos{0};
        static constexpr float kFeedback = 0.5f;

        void resize(int n) { buf.assign(n, 0.0f); }
        void reset() { std::fill(buf.begin(), buf.end(), 0.0f); writePos = 0; }

        float process(float in)
        {
            int   sz      = static_cast<int>(buf.size());
            float bufOut  = buf[writePos];
            float output  = -in + bufOut;
            buf[writePos] = in + bufOut * kFeedback;
            writePos      = (writePos + 1) % sz;
            return output;
        }
    };

    static constexpr int kNumCombs    = 8;
    static constexpr int kNumAllPasses = 4;

    // Stereo: separate L/R comb + allpass banks
    std::array<CombFilter,    kNumCombs>     mCombL, mCombR;
    std::array<AllPassFilter, kNumAllPasses> mApL,   mApR;

    // Freeverb delay lengths at 44100 Hz
    static constexpr int kCombLengths[kNumCombs]    = {1116,1188,1277,1356,1422,1491,1557,1617};
    static constexpr int kApLengths[kNumAllPasses]  = {556, 441, 341, 225};
    static constexpr int kStereoOffset              = 23; // samples offset for R channel

    void allocate();
};
