#include "EQProcessor.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

EQProcessor::EQProcessor()
{
    rebuildFilters();
}

void EQProcessor::onSampleRateChanged(double /*sr*/)
{
    // Same lock discipline as the set*Gain setters: the audio thread try_locks
    // this mutex, so coefficient/state rewrites must never happen bare.
    std::lock_guard<std::mutex> lock(mMutex);
    rebuildFilters();
    reset();
}

void EQProcessor::setLowGain(double dB)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mLowGainDB = dB;
    auto bq = makeLowShelf(mSampleRate, 250.0, dB);
    mLowShelfL.setCoeffs(bq);
    mLowShelfR.setCoeffs(bq);
}

void EQProcessor::setMidGain(double dB)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mMidGainDB = dB;
    auto bq = makePeaking(mSampleRate, 1000.0, dB, 0.707);
    mMidPeakL.setCoeffs(bq);
    mMidPeakR.setCoeffs(bq);
}

void EQProcessor::setTrebleGain(double dB)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mTrebleGainDB = dB;
    auto bq = makeHighShelf(mSampleRate, 4000.0, dB);
    mHighShelfL.setCoeffs(bq);
    mHighShelfR.setCoeffs(bq);
}

bool EQProcessor::process(float* L, float* R, int n, bool inputIsStereo)
{
    if (!mEnabled.load())
        return inputIsStereo;

    // Skip this block if the GUI thread is updating coefficients (inaudible at normal buffer sizes)
    if (!mMutex.try_lock())
        return inputIsStereo;

    for (int i = 0; i < n; ++i)
        L[i] = processSampleL(L[i]);

    if (inputIsStereo)
        for (int i = 0; i < n; ++i)
            R[i] = processSampleR(R[i]);
    else
        std::copy(L, L + n, R);

    mMutex.unlock();
    return inputIsStereo;
}

void EQProcessor::reset()
{
    mLowShelfL.reset(); mMidPeakL.reset(); mHighShelfL.reset();
    mLowShelfR.reset(); mMidPeakR.reset(); mHighShelfR.reset();
}

void EQProcessor::rebuildFilters()
{
    auto ls = makeLowShelf(mSampleRate,  250.0, mLowGainDB);
    auto mp = makePeaking (mSampleRate, 1000.0, mMidGainDB, 0.707);
    auto hs = makeHighShelf(mSampleRate, 4000.0, mTrebleGainDB);

    mLowShelfL.setCoeffs(ls); mLowShelfR.setCoeffs(ls);
    mMidPeakL .setCoeffs(mp); mMidPeakR .setCoeffs(mp);
    mHighShelfL.setCoeffs(hs); mHighShelfR.setCoeffs(hs);
}

EQProcessor::Biquad EQProcessor::makeLowShelf(double sampleRate, double freq, double gainDB)
{
    Biquad bq;
    double A     = std::pow(10.0, gainDB / 40.0);
    double w0    = 2.0 * M_PI * freq / sampleRate;
    double cosW  = std::cos(w0);
    double sinW  = std::sin(w0);
    double alpha = sinW / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / 1.0 - 1.0) + 2.0);

    double b0 =      A * ((A + 1.0) - (A - 1.0) * cosW + 2.0 * std::sqrt(A) * alpha);
    double b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosW);
    double b2 =      A * ((A + 1.0) - (A - 1.0) * cosW - 2.0 * std::sqrt(A) * alpha);
    double a0 =           (A + 1.0) + (A - 1.0) * cosW + 2.0 * std::sqrt(A) * alpha;
    double a1 =    -2.0 * ((A - 1.0) + (A + 1.0) * cosW);
    double a2 =           (A + 1.0) + (A - 1.0) * cosW - 2.0 * std::sqrt(A) * alpha;

    bq.b0 = b0/a0; bq.b1 = b1/a0; bq.b2 = b2/a0;
    bq.a1 = a1/a0; bq.a2 = a2/a0;
    return bq;
}

EQProcessor::Biquad EQProcessor::makePeaking(double sampleRate, double freq, double gainDB, double Q)
{
    Biquad bq;
    double A     = std::pow(10.0, gainDB / 40.0);
    double w0    = 2.0 * M_PI * freq / sampleRate;
    double alpha = std::sin(w0) / (2.0 * Q);

    double b0 =  1.0 + alpha * A;
    double b1 = -2.0 * std::cos(w0);
    double b2 =  1.0 - alpha * A;
    double a0 =  1.0 + alpha / A;
    double a2 =  1.0 - alpha / A;

    bq.b0 = b0/a0; bq.b1 = b1/a0; bq.b2 = b2/a0;
    bq.a1 = b1/a0; bq.a2 = a2/a0;
    return bq;
}

EQProcessor::Biquad EQProcessor::makeHighShelf(double sampleRate, double freq, double gainDB)
{
    Biquad bq;
    double A     = std::pow(10.0, gainDB / 40.0);
    double w0    = 2.0 * M_PI * freq / sampleRate;
    double cosW  = std::cos(w0);
    double sinW  = std::sin(w0);
    double alpha = sinW / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / 1.0 - 1.0) + 2.0);

    double b0 =       A * ((A + 1.0) + (A - 1.0) * cosW + 2.0 * std::sqrt(A) * alpha);
    double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosW);
    double b2 =       A * ((A + 1.0) + (A - 1.0) * cosW - 2.0 * std::sqrt(A) * alpha);
    double a0 =            (A + 1.0) - (A - 1.0) * cosW + 2.0 * std::sqrt(A) * alpha;
    double a1 =   2.0 * ((A - 1.0) - (A + 1.0) * cosW);
    double a2 =            (A + 1.0) - (A - 1.0) * cosW - 2.0 * std::sqrt(A) * alpha;

    bq.b0 = b0/a0; bq.b1 = b1/a0; bq.b2 = b2/a0;
    bq.a1 = a1/a0; bq.a2 = a2/a0;
    return bq;
}
