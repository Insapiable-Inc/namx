#include "TunerProcessor.h"

#include <cmath>
#include <algorithm>
#include <cstring>

TunerProcessor::TunerProcessor()
{
    mRingBuf.fill(0.f);
    mSnapBuf.fill(0.f);
}

void TunerProcessor::reset()
{
    mRingBuf.fill(0.f);
    mWritePos.store(0);
    mBufferReady.store(false);
}

void TunerProcessor::processBlock(float* buf, int nframes, bool mute)
{
    int wp = mWritePos.load();
    for (int i = 0; i < nframes; ++i)
    {
        mRingBuf[wp] = buf[i];
        wp = (wp + 1) % kBufSize;
        if (mute) buf[i] = 0.0f;
    }
    mWritePos.store(wp);
    mBufferReady.store(true);
}

float TunerProcessor::detectPitch()
{
    if (!mBufferReady.load())
        return 0.0f;

    // Copy ring buffer into contiguous snapshot starting from oldest sample
    int startPos = mWritePos.load();
    for (int i = 0; i < kBufSize; ++i)
        mSnapBuf[i] = mRingBuf[(startPos + i) % kBufSize];

    // Simple autocorrelation pitch detection
    // Search period range corresponding to 50 Hz – 2000 Hz
    const int minPeriod = static_cast<int>(mSampleRate / 2000.0);
    const int maxPeriod = static_cast<int>(mSampleRate / 50.0);

    if (maxPeriod >= kBufSize / 2)
        return 0.0f;

    // Normalised autocorrelation
    double rms = 0.0;
    for (int i = 0; i < kBufSize; ++i)
        rms += mSnapBuf[i] * mSnapBuf[i];
    rms = std::sqrt(rms / kBufSize);

    if (rms < 0.001)   // silence
        return 0.0f;

    double bestCorr = -1.0;
    int    bestLag  = 0;

    for (int lag = minPeriod; lag <= maxPeriod; ++lag)
    {
        double corr = 0.0;
        int    N    = kBufSize - lag;
        for (int i = 0; i < N; ++i)
            corr += mSnapBuf[i] * mSnapBuf[i + lag];
        corr /= N;

        if (corr > bestCorr)
        {
            bestCorr = corr;
            bestLag  = lag;
        }
    }

    if (bestLag == 0 || bestCorr < 0.01)
        return 0.0f;

    // Parabolic interpolation for sub-sample accuracy
    if (bestLag > minPeriod && bestLag < maxPeriod)
    {
        int    N   = kBufSize - bestLag;
        double y0  = 0.0, y1 = 0.0, y2 = 0.0;
        for (int i = 0; i < N; ++i)
        {
            y1 += mSnapBuf[i] * mSnapBuf[i + bestLag];
            if (bestLag > 1)
                y0 += mSnapBuf[i] * mSnapBuf[i + bestLag - 1];
            y2 += mSnapBuf[i] * mSnapBuf[i + bestLag + 1];
        }
        y0 /= N; y1 /= N; y2 /= N;
        double denom = 2.0 * (2.0 * y1 - y0 - y2);
        if (std::abs(denom) > 1e-10)
        {
            double delta = (y0 - y2) / denom;
            bestLag += static_cast<int>(std::round(delta * 10.0)) / 10;
        }
    }

    return static_cast<float>(mSampleRate / bestLag);
}

int TunerProcessor::freqToMidi(float freq)
{
    if (freq <= 0.0f) return -1;
    return static_cast<int>(std::round(69.0f + 12.0f * std::log2(freq / 440.0f)));
}

std::string TunerProcessor::midiToName(int midiNote)
{
    if (midiNote < 0) return "--";
    static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    int octave = midiNote / 12 - 1;
    int note   = midiNote % 12;
    return std::string(names[note]) + std::to_string(octave);
}

float TunerProcessor::centsOffset(float freq)
{
    if (freq <= 0.0f) return 0.0f;
    int   midiNote   = freqToMidi(freq);
    float noteFreq   = 440.0f * std::pow(2.0f, (midiNote - 69) / 12.0f);
    return 1200.0f * std::log2(freq / noteFreq);
}
