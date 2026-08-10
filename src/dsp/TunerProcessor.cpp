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
    std::lock_guard<std::mutex> lock(mBufMutex);
    mRingBuf.fill(0.f);
    mWritePos    = 0;
    mBufferReady = false;
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

    const int fs     = static_cast<int>(mFs.load());
    const int minLag = fs / 1400;
    const int maxLag = fs / 50;
    const int N      = kBufSize;

    float bestNSDF = -1.0f;
    int   bestLag  = 0;

    const int winLen = std::min(N, 1024);

    double r0 = 0.0;
    for (int i = 0; i < winLen; ++i)
        r0 += mSnapBuf[i] * mSnapBuf[i];

    for (int lag = minLag; lag <= std::min(maxLag, N - winLen - 1); ++lag)
    {
        double r = 0.0, mk = 0.0;
        for (int i = 0; i < winLen; ++i)
        {
            r  += mSnapBuf[i] * mSnapBuf[i + lag];
            mk += mSnapBuf[i + lag] * mSnapBuf[i + lag];
        }
        double denom = r0 + mk;
        float  nsdf  = (denom > 1e-12) ? static_cast<float>(2.0 * r / denom) : 0.0f;
        if (nsdf > bestNSDF) { bestNSDF = nsdf; bestLag = lag; }
    }

    if (bestNSDF < 0.40f || bestLag == 0)
        return 0.0f;

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
            float delta = std::max(-1.0f, std::min(1.0f, (y0 - y2) / denom));
            return mFs.load() / (bestLag + delta);
        }
    }

    return mFs.load() / bestLag;
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
