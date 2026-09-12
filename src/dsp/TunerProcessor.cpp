#include "TunerProcessor.h"

#include <algorithm>
#include <chrono>
#include <cmath>

TunerProcessor::TunerProcessor()
{
    mRingBuf.fill(0.f);
    mSnapBuf.fill(0.f);
    mWorker = std::thread(&TunerProcessor::workerMain, this);
}

TunerProcessor::~TunerProcessor()
{
    {
        std::lock_guard<std::mutex> lock(mWorkMutex);
        mStop = true;
    }
    mWorkCv.notify_all();
    if (mWorker.joinable())
        mWorker.join();
}

void TunerProcessor::workerMain()
{
    std::unique_lock<std::mutex> lk(mWorkMutex);
    while (!mStop)
    {
        mWorkCv.wait_for(lk, std::chrono::milliseconds(100));
        if (mStop)
            break;
        if (!isEnabled())
        {
            mLastPitch.store(0.0f);
            continue;
        }
        lk.unlock();
        mLastPitch.store(detectPitch());
        lk.lock();
    }
}

void TunerProcessor::reset()
{
    // May run on the audio thread (chain reorder) — never block. If the
    // worker is mid-snapshot, the ring keeps its stale contents; it is
    // overwritten within a few blocks anyway.
    if (mBufMutex.try_lock())
    {
        mRingBuf.fill(0.f);
        mWritePos    = 0;
        mBufferReady = false;
        mBufMutex.unlock();
    }
}

void TunerProcessor::processBlockMono(float* buf, int nframes)
{
    // try_lock: if the GUI thread is currently snapshotting, skip the write for this
    // block. A ~5 ms gap in ring-buffer data is completely unnoticeable in a tuner display.
    if (mBufMutex.try_lock())
    {
        for (int i = 0; i < nframes; ++i)
        {
            mRingBuf[mWritePos] = buf[i];
            mWritePos = (mWritePos + 1) % kBufSize;
        }
        mBufferReady = true;
        mBufMutex.unlock();
    }

    if (mMute.load())
        std::fill(buf, buf + nframes, 0.0f);
}

void TunerProcessor::processBlockDual(float* L, float* R, int nframes)
{
    if (mBufMutex.try_lock())
    {
        for (int i = 0; i < nframes; ++i)
        {
            mRingBuf[mWritePos] = 0.5f * (L[i] + R[i]);
            mWritePos = (mWritePos + 1) % kBufSize;
        }
        mBufferReady = true;
        mBufMutex.unlock();
    }

    if (mMute.load())
    {
        std::fill(L, L + nframes, 0.0f);
        std::fill(R, R + nframes, 0.0f);
    }
}

float TunerProcessor::detectPitch()
{
    {
        std::lock_guard<std::mutex> lock(mBufMutex);
        if (!mBufferReady)
            return 0.0f;

        for (int i = 0; i < kBufSize; ++i)
            mSnapBuf[i] = mRingBuf[(mWritePos + i) % kBufSize];
    }

    double rmsSum = 0.0;
    for (int i = 0; i < kBufSize; ++i)
        rmsSum += mSnapBuf[i] * mSnapBuf[i];
    if (std::sqrt(rmsSum / kBufSize) < 5e-4)
        return 0.0f;

    const float fs     = mFs.load();
    const int   N      = kBufSize;
    const int   winLen = std::min(N, 1024);
    const int   minLag = std::max(2, static_cast<int>(fs / 1400.0f)); // ~1400 Hz ceiling
    const int   maxLag = std::min(static_cast<int>(fs / 50.0f), N - winLen - 1); // ~50 Hz floor
    if (maxLag <= minLag)
        return 0.0f;

    // NSDF (McLeod Pitch Method): n(τ) = 2·r(τ) / (m0 + m(τ)) over a fixed
    // 1024-sample window, evaluated for every lag from 1 so the first
    // positive lobe (around τ = 0) can be skipped explicitly below.
    double r0 = 0.0;
    for (int i = 0; i < winLen; ++i)
        r0 += mSnapBuf[i] * mSnapBuf[i];

    mNsdf.assign(maxLag + 2, 0.0f);
    mNsdf[0] = 1.0f;
    for (int lag = 1; lag <= maxLag + 1; ++lag)
    {
        double r = 0.0, mk = 0.0;
        for (int i = 0; i < winLen; ++i)
        {
            r  += mSnapBuf[i] * mSnapBuf[i + lag];
            mk += mSnapBuf[i + lag] * mSnapBuf[i + lag];
        }
        const double denom = r0 + mk;
        mNsdf[lag] = (denom > 1e-12) ? static_cast<float>(2.0 * r / denom) : 0.0f;
    }

    // Key-maxima picking. The global NSDF maximum is unreliable: for a
    // periodic signal every multiple of the period scores ~1.0, so the
    // global max regularly lands on 2T, 3T… (an octave or more too low).
    // MPM instead takes the highest point of each positive lobe after the
    // first negative crossing, then picks the FIRST such peak that is within
    // a fraction k of the tallest — that is the fundamental period.
    constexpr float kKeyMaxRatio = 0.8f;

    int lag = 1;
    while (lag <= maxLag && mNsdf[lag] > 0.0f) ++lag; // skip the τ≈0 lobe

    float bestVal  = -1.0f;
    int   pickLag  = 0;
    float pickVal  = -1.0f;
    struct Key { int lag; float val; };
    Key keys[64];
    int nKeys = 0;

    while (lag <= maxLag && nKeys < 64)
    {
        while (lag <= maxLag && mNsdf[lag] <= 0.0f) ++lag;
        if (lag > maxLag) break;
        int   peakLag = lag;
        float peakVal = mNsdf[lag];
        while (lag <= maxLag && mNsdf[lag] > 0.0f)
        {
            if (mNsdf[lag] > peakVal) { peakVal = mNsdf[lag]; peakLag = lag; }
            ++lag;
        }
        if (peakLag < minLag) continue;
        keys[nKeys++] = {peakLag, peakVal};
        bestVal = std::max(bestVal, peakVal);
    }

    if (nKeys == 0 || bestVal < 0.40f)
        return 0.0f;

    const float threshold = kKeyMaxRatio * bestVal;
    for (int i = 0; i < nKeys; ++i)
    {
        if (keys[i].val >= threshold) { pickLag = keys[i].lag; pickVal = keys[i].val; break; }
    }
    if (pickLag <= 0)
        return 0.0f;

    // Parabolic interpolation through (lag-1, lag, lag+1). With
    // y = a·x² + b·x + c fitted at x = -1, 0, +1 the vertex sits at
    // x = (y2 - y0) / (2·(2·y1 - y0 - y2)).
    if (pickLag >= 1 && pickLag + 1 <= maxLag + 1)
    {
        const float y0    = mNsdf[pickLag - 1];
        const float y1    = pickVal;
        const float y2    = mNsdf[pickLag + 1];
        const float denom = 2.0f * (2.0f * y1 - y0 - y2);
        if (std::abs(denom) > 1e-6f)
        {
            const float delta = std::max(-1.0f, std::min(1.0f, (y2 - y0) / denom));
            return fs / (static_cast<float>(pickLag) + delta);
        }
    }

    return fs / static_cast<float>(pickLag);
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
    return std::max(-50.0f, std::min(50.0f, 1200.0f * std::log2(freq / noteFreq)));
}
