#include "AudioEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

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

    // Stable effect IDs in default processing order — the contract for chain
    // reordering and for session/preset persistence. Never rename an ID.
    mEffectTable = {
        {"tuner",   &mTuner},
        {"gate",    &mGate},
        {"comp",    &mComp},
        {"wah",     &mWah},
        {"flanger", &mFlanger},
        {"phaser",  &mPhaser},
        {"nam",     &mNam},
        {"eq",      &mEQ},
        {"chorus",  &mChorus},
        {"tremolo", &mTremolo},
        {"delay",   &mDelay},
        {"reverb",  &mReverb},
        {"ir",      &mIR},
    };
    mChainBuf[0].reserve(mEffectTable.size());
    for (const auto& [id, fx] : mEffectTable)
        mChainBuf[0].push_back(fx);
    mChainBuf[1] = mChainBuf[0];
}

// ---------------------------------------------------------------------------
// Chain order
// ---------------------------------------------------------------------------

std::vector<std::string> AudioEngine::defaultChainIds() const
{
    std::vector<std::string> ids;
    ids.reserve(mEffectTable.size());
    for (const auto& [id, fx] : mEffectTable)
        ids.push_back(id);
    return ids;
}

std::vector<std::string> AudioEngine::chainIds() const
{
    std::vector<std::string> ids;
    ids.reserve(mEffectTable.size());
    for (const auto* fx : chain())
        ids.push_back(idForEffect(fx));
    return ids;
}

EffectBase* AudioEngine::effectById(const std::string& id) const
{
    for (const auto& [eid, fx] : mEffectTable)
        if (eid == id)
            return fx;
    return nullptr;
}

std::string AudioEngine::idForEffect(const EffectBase* fx) const
{
    for (const auto& [eid, e] : mEffectTable)
        if (e == fx)
            return eid;
    return {};
}

void AudioEngine::setChainOrder(const std::vector<std::string>& ids)
{
    // Sanitize: known IDs only, no duplicates.
    std::vector<EffectBase*> newChain;
    newChain.reserve(mEffectTable.size());
    for (const auto& id : ids)
    {
        EffectBase* fx = effectById(id);
        if (!fx) continue;
        if (std::find(newChain.begin(), newChain.end(), fx) != newChain.end()) continue;
        newChain.push_back(fx);
    }

    // Effects missing from the list re-enter at their default position.
    int tableIdx = 0;
    for (const auto& [id, fx] : mEffectTable)
    {
        const size_t defaultIdx = tableIdx++;
        if (std::find(newChain.begin(), newChain.end(), fx) == newChain.end())
            newChain.insert(newChain.begin() + std::min(defaultIdx, newChain.size()), fx);
    }

    const int active   = mActiveChain.load(std::memory_order_relaxed);
    const int inactive = 1 - active;
    const auto& cur    = mChainBuf[active];
    if (newChain == cur)
        return;

    // The audio thread may still be iterating the buffer we are about to
    // write if the previous swap happened less than one callback ago. Two
    // callback completions after a swap guarantee it has left; bounded wait
    // so a stalled server can't hang the GUI.
    if (isRunning())
    {
        const uint64_t safe = mLastSwapCallbackCount + 2;
        for (int i = 0; i < 100 && isRunning()
                        && mCallbackCount.load(std::memory_order_acquire) < safe; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // Moved effects carry state from their old slot (delay tails, diverged
    // per-channel filters) — have the audio thread clear them.
    for (size_t i = 0; i < newChain.size(); ++i)
        if (i >= cur.size() || cur[i] != newChain[i])
            newChain[i]->requestReset();

    mChainBuf[inactive] = std::move(newChain);
    mActiveChain.store(inactive, std::memory_order_release);
    mLastSwapCallbackCount = mCallbackCount.load(std::memory_order_acquire);
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

    // Sample rate and buffer size reach the effects via onBufferSizeChanged,
    // which the backend invokes BEFORE activating the stream — configuring
    // effects after activation would race the audio thread.
    if (!mBackend->start(clientName, this)) {
        mBackend.reset();
        return false;
    }

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

    // Backends call this before activating the stream, and with processing
    // suspended on later changes — the one safe point to reconfigure effects.
    const double sr = mBackend ? mBackend->sampleRate() : 0.0;
    if (sr > 0.0 && sr != mChainSampleRate) {
        mChainSampleRate = sr;
        for (const auto& [id, fx] : mEffectTable)
            fx->setSampleRate(sr);
        // ~20 ms one-pole gain smoothing, independent of sample rate.
        mSmoothCoeff = std::exp(-1.0f / (0.020f * static_cast<float>(sr)));
    }

    // After SR propagation, so a re-Reset() picks up the new rate.
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
        mInputGainSmooth = mSmoothCoeff * mInputGainSmooth
                         + (1.0f - mSmoothCoeff) * targetInGain;
        float s = in[i] * mInputGainSmooth;
        mWorkBufL[i] = s;
        mWorkBufR[i] = s;
        inPeak = std::max(inPeak, std::abs(s));
    }
    // Peak-hold: accumulate the max until the GUI takes and resets it, so the
    // 50 ms poll can't miss transients between blocks.
    if (inPeak > mInputPeak.load(std::memory_order_relaxed))
        mInputPeak.store(inPeak, std::memory_order_relaxed);

    // 2. Run the signal chain (chain pointer read once per callback; reorders
    //    swap the buffer index and never touch the one being iterated)
    bool stereo = false;
    const auto& chain = mChainBuf[mActiveChain.load(std::memory_order_acquire)];
    for (auto* fx : chain)
    {
        fx->consumePendingReset();
        stereo = fx->process(mWorkBufL.data(), mWorkBufR.data(), n, stereo);
    }

    (void)stereo; // consumed by downstream effects; output always written to both channels

    // 3. Output gain (smoothed) + write to output buffers
    const float targetOutGain = dbToLinear(mOutputGainDB.load());
    float outPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        mOutputGainSmooth = mSmoothCoeff * mOutputGainSmooth
                          + (1.0f - mSmoothCoeff) * targetOutGain;
        outL[i] = mWorkBufL[i] * mOutputGainSmooth;
        outR[i] = mWorkBufR[i] * mOutputGainSmooth;
        outPeak = std::max(outPeak, std::max(std::abs(outL[i]), std::abs(outR[i])));
    }
    if (outPeak > mOutputPeak.load(std::memory_order_relaxed))
        mOutputPeak.store(outPeak, std::memory_order_relaxed);

    // Reorder fence: setChainOrder counts completed callbacks to know when
    // the retired chain buffer is safe to overwrite.
    mCallbackCount.fetch_add(1, std::memory_order_release);
}
