#pragma once

#include <array>
#include <cmath>

/// Simple 3-band parametric EQ implemented as biquad filters.
/// Bands: Low shelf, Mid peak, High shelf
class EQProcessor
{
public:
    EQProcessor();

    void setSampleRate(double sampleRate);

    /// Gain in dB, range typically -12 to +12
    void setLowGain(double dB);
    void setMidGain(double dB);
    void setTrebleGain(double dB);

    /// Process a single mono sample in-place
    float processSample(float in);

    /// Reset filter state
    void reset();

private:
    struct Biquad
    {
        // Coefficients
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        // State
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

        float process(float in)
        {
            double x0 = in;
            double y0 = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x0;
            y2 = y1; y1 = y0;
            return static_cast<float>(y0);
        }

        void reset() { x1 = x2 = y1 = y2 = 0.0; }
    };

    /// Design a low-shelf biquad at the given frequency
    static Biquad makeLowShelf(double sampleRate, double freq, double gainDB);
    /// Design a peaking EQ biquad at the given frequency
    static Biquad makePeaking(double sampleRate, double freq, double gainDB, double Q);
    /// Design a high-shelf biquad at the given frequency
    static Biquad makeHighShelf(double sampleRate, double freq, double gainDB);

    double mSampleRate = 44100.0;
    double mLowGainDB  = 0.0;
    double mMidGainDB  = 0.0;
    double mTrebleGainDB = 0.0;

    Biquad mLowShelf;
    Biquad mMidPeak;
    Biquad mHighShelf;

    void rebuildFilters();
};
