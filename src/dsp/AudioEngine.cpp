#include "AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <iostream>

AudioEngine::AudioEngine()
{
    // Effects that are naturally stereo default to stereo mode on.
    // Flanger/Phaser stereo mode is user-controlled (default off).
    mChorus.setStereoMode(true);
    mTremolo.setStereoMode(true);
    mReverb .setStereoMode(true);
    // Delay stereo (ping-pong) is on by default to match original behaviour.
    mDelay.setStereoMode(true);

    // NAM and Tuner start enabled; all others start disabled.
    mNam  .setEnabled(true);
    mTuner.setEnabled(false);
    mEQ   .setEnabled(true);
    mIR   .setEnabled(true);

    // Build the signal chain in processing order.
    mChain = {
        &mTuner,
        &mGate,
        &mComp,
        &mWah,
        &mFlanger,
        &mPhaser,
        &mNam,
        &mEQ,
        &mChorus,
        &mTremolo,
        &mDelay,
        &mReverb,
        &mIR
    };
}

AudioEngine::~AudioEngine()
{
    stop();
}

bool AudioEngine::start(const std::string& clientName)
{
    if (mBackend && mBackend->isRunning()) return true;

    mBackend = IAudioBackend::create();
    if (!mBackend) {
        std::cerr << "[AudioEngine] No audio backend available for this platform\n";
        return false;
    }

    if (!mBackend->start(clientName, this)) {
        mBackend.reset();
        return false;
    }

    // Propagate sample rate to all effects now that the backend is open.
    const double sr = mBackend->sampleRate();
    for (auto* fx : mChain)
        fx->setSampleRate(sr);

    return true;
}

bool AudioEngine::startWithDevices(const std::string& inputDeviceId,
                                   const std::string& outputDeviceId,
                                   const std::string& clientName)
{
    stop();

    mBackend = IAudioBackend::create();
    if (!mBackend) {
        std::cerr << "[AudioEngine] No audio backend available for this platform\n";
        return false;
    }

    if (!mBackend->startWithDevices(clientName, this, inputDeviceId, outputDeviceId)) {
        mBackend.reset();
        return false;
    }

    const double sr = mBackend->sampleRate();
    for (auto* fx : mChain)
        fx->setSampleRate(sr);

    return true;
}

void AudioEngine::stop()
{
    if (mBackend) {
        mBackend->stop();
        mBackend.reset();
    }
}

std::vector<IAudioBackend::DeviceInfo> AudioEngine::enumerateInputDevices() const
{
    auto temp = IAudioBackend::create();
    return temp ? temp->enumerateInputDevices() : std::vector<IAudioBackend::DeviceInfo>{};
}

std::vector<IAudioBackend::DeviceInfo> AudioEngine::enumerateOutputDevices() const
{
    auto temp = IAudioBackend::create();
    return temp ? temp->enumerateOutputDevices() : std::vector<IAudioBackend::DeviceInfo>{};
}

// ---- IAudioBackend::Callback ----------------------------------------

void AudioEngine::onBufferSizeChanged(int n)
{
    if (n <= 0) return;
    if (n > static_cast<int>(mWorkBufL.size())) {
        mWorkBufL.resize(n, 0.0f);
        mWorkBufR.resize(n, 0.0f);
    }
    mNam.prepareBuffer(std::max(n, mNam.maxBuf()));
}

void AudioEngine::onAudio(const float* in, float* outL, float* outR, int n)
{
    // Defensive: if a process callback ever fires before onBufferSizeChanged
    // has grown the work buffers, emit silence rather than overrunning.
    if (static_cast<size_t>(n) > mWorkBufL.size())
    {
        std::fill_n(outL, n, 0.0f);
        std::fill_n(outR, n, 0.0f);
        return;
    }

    // 1. Input gain (smoothed) → both work buffers (identical at this point)
    const float targetInGain = dbToLinear(mInputGainDB.load());
    float inPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        mInputGainSmooth = kSmoothCoeff * mInputGainSmooth
                         + (1.0f - kSmoothCoeff) * targetInGain;
        float s = in[i] * mInputGainSmooth;
        mWorkBufL[i] = s;
        mWorkBufR[i] = s;
        inPeak = std::max(inPeak, std::abs(s));
    }
    mInputPeak.store(inPeak);

    // 2. Run the signal chain
    bool stereo = false;
    for (auto* fx : mChain)
        stereo = fx->process(mWorkBufL.data(), mWorkBufR.data(), n, stereo);

    (void)stereo; // consumed by downstream effects; output always written to both channels

    // 3. Output gain (smoothed) + write to output buffers
    const float targetOutGain = dbToLinear(mOutputGainDB.load());
    float outPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        mOutputGainSmooth = kSmoothCoeff * mOutputGainSmooth
                          + (1.0f - kSmoothCoeff) * targetOutGain;
        outL[i] = mWorkBufL[i] * mOutputGainSmooth;
        outR[i] = mWorkBufR[i] * mOutputGainSmooth;
        outPeak = std::max(outPeak, std::max(std::abs(outL[i]), std::abs(outR[i])));
    }
    mOutputPeak.store(outPeak);
}
