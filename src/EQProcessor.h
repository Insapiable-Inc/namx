#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <mutex>

#include "EffectBase.h"

/// 3-band parametric EQ (low shelf 250 Hz, mid peak 1 kHz, high shelf 4 kHz).
/// Uses a try_lock in the audio thread so GUI coefficient updates never stall the callback.
/// Maintains independent L/R biquad state for stereo processing.
class EQProcessor : public EffectBase
{
public:
    EQProcessor();

    /// Override: try_lock the internal mutex before processing; skip block if unavailable.
    bool process(float* L, float* R, int n, bool inputIsStereo) override;

    void reset() override;

    void setLowGain(double dB);
    void setMidGain(double dB);
    void setTrebleGain(double dB);

    double lowGainDB()    const { return mLowGainDB;    }
    double midGainDB()    const { return mMidGainDB;    }
    double trebleGainDB() const { return mTrebleGainDB; }

protected:
    void onSampleRateChanged(double sr) override;

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

        float process(float in)
        {
            double x0 = in;
            double y0 = b0*x0 + b1*x1 + b2*x2 - a1*y1 - a2*y2;
            x2 = x1; x1 = x0;
            y2 = y1; y1 = y0;
            return static_cast<float>(y0);
        }

        void reset() { x1 = x2 = y1 = y2 = 0.0; }

        void setCoeffs(const Biquad& src)
        {
            b0 = src.b0; b1 = src.b1; b2 = src.b2;
            a1 = src.a1; a2 = src.a2;
            // state left intact
        }
    };

    static Biquad makeLowShelf(double sampleRate, double freq, double gainDB);
    static Biquad makePeaking(double sampleRate, double freq, double gainDB, double Q);
    static Biquad makeHighShelf(double sampleRate, double freq, double gainDB);

    double mLowGainDB    = 0.0;
    double mMidGainDB    = 0.0;
    double mTrebleGainDB = 0.0;

    // Independent L/R biquad state (same coefficients, separate delay lines)
    Biquad mLowShelfL, mMidPeakL, mHighShelfL;
    Biquad mLowShelfR, mMidPeakR, mHighShelfR;

    std::mutex mMutex; // GUI writes coefficients; audio thread uses try_lock

    void rebuildFilters();

    float processSampleL(float in) { return mHighShelfL.process(mMidPeakL.process(mLowShelfL.process(in))); }
    float processSampleR(float in) { return mHighShelfR.process(mMidPeakR.process(mLowShelfR.process(in))); }
};
