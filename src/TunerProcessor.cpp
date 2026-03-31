#include "TunerProcessor.h"

#include <cmath>
#include <algorithm>

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
    int wp = mWritePos.load(std::memory_order_relaxed);
    for (int i = 0; i < nframes; ++i)
    {
        mRingBuf[wp] = buf[i];
        wp = (wp + 1) % kBufSize;
        if (mute) buf[i] = 0.0f;
    }
    mWritePos.store(wp, std::memory_order_release);
    mBufferReady.store(true, std::memory_order_release);
}

float TunerProcessor::detectPitch()
{
    if (!mBufferReady.load(std::memory_order_acquire))
        return 0.0f;

    // Copy ring buffer into contiguous snapshot (oldest → newest)
    const int startPos = mWritePos.load(std::memory_order_acquire);
    for (int i = 0; i < kBufSize; ++i)
        mSnapBuf[i] = mRingBuf[(startPos + i) % kBufSize];

    // RMS check — silence guard
    double rmsSum = 0.0;
    for (int i = 0; i < kBufSize; ++i)
        rmsSum += mSnapBuf[i] * mSnapBuf[i];
    const double rms = std::sqrt(rmsSum / kBufSize);
    if (rms < 5e-4)
        return 0.0f;

    // Guitar range: E2 (82 Hz) to E6 (1319 Hz)
    const int fs      = static_cast<int>(mSampleRate);
    const int minLag  = fs / 1400;       // ~31 samples at 44100
    const int maxLag  = fs / 50;         // ~882 samples at 44100
    const int N       = kBufSize;

    // --- Normalized Square Difference Function (NSDF / YIN-style) ---
    // nsdf[k] = 2*r[k] / (m0 + mk)
    // where r[k] = correlation, m0 = energy of first half, mk = energy of second half.
    // nsdf ranges from -1 to +1; 1 = perfect periodicity.

    // Pre-compute running energy sums using a sliding window
    // m0 = sum x[n]^2 for n in [0, N-lag)
    // mk = sum x[n+lag]^2 for n in [0, N-lag)

    float bestNSDF = -1.0f;
    int   bestLag  = 0;

    // Use a reasonable window size to avoid O(N^2) being too slow
    const int winLen = std::min(N, 1024);

    double r0 = 0.0;
    for (int i = 0; i < winLen; ++i)
        r0 += mSnapBuf[i] * mSnapBuf[i];

    for (int lag = minLag; lag <= std::min(maxLag, N - winLen - 1); ++lag)
    {
        double r  = 0.0;
        double mk = 0.0;
        for (int i = 0; i < winLen; ++i)
        {
            r  += mSnapBuf[i] * mSnapBuf[i + lag];
            mk += mSnapBuf[i + lag] * mSnapBuf[i + lag];
        }

        double denom = r0 + mk;
        float  nsdf  = (denom > 1e-12) ? static_cast<float>(2.0 * r / denom) : 0.0f;

        if (nsdf > bestNSDF)
        {
            bestNSDF = nsdf;
            bestLag  = lag;
        }
    }

    // Require at least 40% periodicity
    if (bestNSDF < 0.40f || bestLag == 0)
        return 0.0f;

    // Parabolic interpolation around bestLag for sub-sample accuracy
    if (bestLag > minLag && bestLag < std::min(maxLag, N - winLen - 2))
    {
        auto nsdfAt = [&](int lag) -> float {
            double r = 0.0, mk = 0.0;
            for (int i = 0; i < winLen; ++i)
            {
                r  += mSnapBuf[i] * mSnapBuf[i + lag];
                mk += mSnapBuf[i + lag] * mSnapBuf[i + lag];
            }
            double d = r0 + mk;
            return (d > 1e-12) ? static_cast<float>(2.0 * r / d) : 0.0f;
        };

        float y0 = nsdfAt(bestLag - 1);
        float y1 = bestNSDF;
        float y2 = nsdfAt(bestLag + 1);

        float denom = 2.0f * (2.0f * y1 - y0 - y2);
        if (std::abs(denom) > 1e-6f)
        {
            float delta = (y0 - y2) / denom;
            // clamp interpolation correction to ±1 sample
            delta = std::max(-1.0f, std::min(1.0f, delta));
            return static_cast<float>(mSampleRate) / (bestLag + delta);
        }
    }

    return static_cast<float>(mSampleRate) / bestLag;
}

int TunerProcessor::freqToMidi(float freq)
{
    if (freq <= 0.0f) return -1;
    return static_cast<int>(std::round(69.0f + 12.0f * std::log2(freq / 440.0f)));
}

std::string TunerProcessor::midiToName(int midiNote)
{
    if (midiNote < 0 || midiNote > 127) return "--";
    static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    int octave = midiNote / 12 - 1;
    return std::string(names[midiNote % 12]) + std::to_string(octave);
}

float TunerProcessor::centsOffset(float freq)
{
    if (freq <= 0.0f) return 0.0f;
    int   midi     = freqToMidi(freq);
    float noteFreq = 440.0f * std::pow(2.0f, (midi - 69) / 12.0f);
    float cents    = 1200.0f * std::log2(freq / noteFreq);
    return std::max(-50.0f, std::min(50.0f, cents));
}
