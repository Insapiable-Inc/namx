#include "EQProcessor.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

EQProcessor::EQProcessor()
{
    rebuildFilters();
}

void EQProcessor::setSampleRate(double sampleRate)
{
    mSampleRate = sampleRate;
    rebuildFilters();
    reset();
}

void EQProcessor::setLowGain(double dB)
{
    mLowGainDB = dB;
    mLowShelf = makeLowShelf(mSampleRate, 250.0, dB);
}

void EQProcessor::setMidGain(double dB)
{
    mMidGainDB = dB;
    mMidPeak = makePeaking(mSampleRate, 1000.0, dB, 0.707);
}

void EQProcessor::setTrebleGain(double dB)
{
    mTrebleGainDB = dB;
    mHighShelf = makeHighShelf(mSampleRate, 4000.0, dB);
}

float EQProcessor::processSample(float in)
{
    float s = mLowShelf.process(in);
    s = mMidPeak.process(s);
    s = mHighShelf.process(s);
    return s;
}

void EQProcessor::reset()
{
    mLowShelf.reset();
    mMidPeak.reset();
    mHighShelf.reset();
}

void EQProcessor::rebuildFilters()
{
    mLowShelf  = makeLowShelf(mSampleRate,  250.0,  mLowGainDB);
    mMidPeak   = makePeaking(mSampleRate,  1000.0,  mMidGainDB, 0.707);
    mHighShelf = makeHighShelf(mSampleRate, 4000.0, mTrebleGainDB);
}

// Audio EQ Cookbook biquad designs
EQProcessor::Biquad EQProcessor::makeLowShelf(double sampleRate, double freq, double gainDB)
{
    Biquad bq;
    double A  = std::pow(10.0, gainDB / 40.0);
    double w0 = 2.0 * M_PI * freq / sampleRate;
    double cosW = std::cos(w0);
    double sinW = std::sin(w0);
    double S  = 1.0; // shelf slope
    double alpha = sinW / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);

    double b0 =     A * ((A + 1.0) - (A - 1.0) * cosW + 2.0 * std::sqrt(A) * alpha);
    double b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosW);
    double b2 =     A * ((A + 1.0) - (A - 1.0) * cosW - 2.0 * std::sqrt(A) * alpha);
    double a0 =          (A + 1.0) + (A - 1.0) * cosW + 2.0 * std::sqrt(A) * alpha;
    double a1 =    -2.0 * ((A - 1.0) + (A + 1.0) * cosW);
    double a2 =          (A + 1.0) + (A - 1.0) * cosW - 2.0 * std::sqrt(A) * alpha;

    bq.b0 = b0 / a0;
    bq.b1 = b1 / a0;
    bq.b2 = b2 / a0;
    bq.a1 = a1 / a0;
    bq.a2 = a2 / a0;
    return bq;
}

EQProcessor::Biquad EQProcessor::makePeaking(double sampleRate, double freq, double gainDB, double Q)
{
    Biquad bq;
    double A  = std::pow(10.0, gainDB / 40.0);
    double w0 = 2.0 * M_PI * freq / sampleRate;
    double alpha = std::sin(w0) / (2.0 * Q);

    double b0 =  1.0 + alpha * A;
    double b1 = -2.0 * std::cos(w0);
    double b2 =  1.0 - alpha * A;
    double a0 =  1.0 + alpha / A;
    double a1 = -2.0 * std::cos(w0);
    double a2 =  1.0 - alpha / A;

    bq.b0 = b0 / a0;
    bq.b1 = b1 / a0;
    bq.b2 = b2 / a0;
    bq.a1 = a1 / a0;
    bq.a2 = a2 / a0;
    return bq;
}

EQProcessor::Biquad EQProcessor::makeHighShelf(double sampleRate, double freq, double gainDB)
{
    Biquad bq;
    double A  = std::pow(10.0, gainDB / 40.0);
    double w0 = 2.0 * M_PI * freq / sampleRate;
    double cosW = std::cos(w0);
    double sinW = std::sin(w0);
    double S  = 1.0;
    double alpha = sinW / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);

    double b0 =      A * ((A + 1.0) + (A - 1.0) * cosW + 2.0 * std::sqrt(A) * alpha);
    double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosW);
    double b2 =      A * ((A + 1.0) + (A - 1.0) * cosW - 2.0 * std::sqrt(A) * alpha);
    double a0 =           (A + 1.0) - (A - 1.0) * cosW + 2.0 * std::sqrt(A) * alpha;
    double a1 =  2.0 * ((A - 1.0) - (A + 1.0) * cosW);
    double a2 =           (A + 1.0) - (A - 1.0) * cosW - 2.0 * std::sqrt(A) * alpha;

    bq.b0 = b0 / a0;
    bq.b1 = b1 / a0;
    bq.b2 = b2 / a0;
    bq.a1 = a1 / a0;
    bq.a2 = a2 / a0;
    return bq;
}
