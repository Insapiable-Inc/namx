#include "AudioEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <NAM/activations.h>

#include "Log.h"

AudioEngine::AudioEngine() {
    // NAM's fast tanh approximation: ~2.4× on Tanh WaveNets (the common
    // architecture), inaudible — the official plugin enables it too. Global
    // to the NAM core, so it covers every model loaded later.
    nam::activations::Activation::enable_fast_tanh();

    // Effects that are naturally stereo default to stereo mode on.
    // Flanger/Phaser stereo mode is user-controlled (default off).
    mChorus.setStereoMode(true);
    mTremolo.setStereoMode(true);
    mReverb.setStereoMode(true);
    // Delay stereo (ping-pong) is on by default to match original behaviour.
    mDelay.setStereoMode(true);

    // NAM and Tuner start enabled; all others start disabled.
    mNam.setEnabled(true);
    mTuner.setEnabled(false);
    mEQ.setEnabled(true);
    mIR.setEnabled(true);

    // Stable effect IDs in default processing order — the contract for chain
    // reordering and for session/preset persistence. Never rename an ID.
    mEffectTable = {
        {"tuner", &mTuner},
        {"transpose", &mTranspose}, // after the tuner: it reads the real string
        {"gate", &mGate},
        {"comp", &mComp},
        {"wah", &mWah},
        {"flanger", &mFlanger},
        {"phaser", &mPhaser},
        {"drive", &mDrive}, // Phase 17 — new IDs re-enter old sessions here
        {"nam", &mNam},
        {"eq", &mEQ},
        {"doubler", &mDoubler},
        {"chorus", &mChorus},
        {"tremolo", &mTremolo},
        {"delay", &mDelay},
        {"reverb", &mReverb},
        {"ir", &mIR},
        {"volume", &mVolume},
    };
    auto chain = std::make_unique<Chain>();
    chain->reserve(mEffectTable.size());
    for (const auto& [id, fx] : mEffectTable) {
        chain->push_back(fx);
    }
    mChain.publish(std::move(chain));
}

// ---------------------------------------------------------------------------
// Model / IR wrappers
// ---------------------------------------------------------------------------

void AudioEngine::reclaimRetired() {
    // Only valid with no audio callback possible: after stop(), or before
    // the backend was ever started.
    const bool stopped = !isRunning();
    mChain.reclaim(stopped);
    mNam.reclaimRetired(stopped);
    mIR.reclaimRetired(stopped);
}

bool AudioEngine::loadModel(const std::string& path) {
    const bool ok = mNam.loadModel(path);
    reclaimRetired();
    return ok;
}

void AudioEngine::unloadModel() {
    mNam.unloadModel();
    reclaimRetired();
}

bool AudioEngine::loadIR(const std::string& path) {
    const bool ok = mIR.load(path);
    reclaimRetired();
    return ok;
}

void AudioEngine::unloadIR() {
    mIR.unload();
    reclaimRetired();
}

bool AudioEngine::loadIRB(const std::string& path) {
    const bool ok = mIR.loadB(path);
    reclaimRetired();
    return ok;
}

void AudioEngine::unloadIRB() {
    mIR.unloadB();
    reclaimRetired();
}

// ---------------------------------------------------------------------------
// Chain order
// ---------------------------------------------------------------------------

std::vector<std::string> AudioEngine::defaultChainIds() const {
    std::vector<std::string> ids;
    ids.reserve(mEffectTable.size());
    for (const auto& [id, fx] : mEffectTable) {
        ids.push_back(id);
    }
    return ids;
}

std::vector<std::string> AudioEngine::chainIds() const {
    std::vector<std::string> ids;
    ids.reserve(mEffectTable.size());
    for (const auto* fx : *mChain.current()) {
        ids.push_back(idForEffect(fx));
    }
    return ids;
}

EffectBase* AudioEngine::effectById(const std::string& id) const {
    for (const auto& [eid, fx] : mEffectTable) {
        if (eid == id) {
            return fx;
        }
    }
    return nullptr;
}

std::string AudioEngine::idForEffect(const EffectBase* fx) const {
    for (const auto& [eid, e] : mEffectTable) {
        if (e == fx) {
            return eid;
        }
    }
    return {};
}

void AudioEngine::setChainOrder(const std::vector<std::string>& ids) {
    // Sanitize: known IDs only, no duplicates.
    std::vector<EffectBase*> newChain;
    newChain.reserve(mEffectTable.size());
    for (const auto& id : ids) {
        EffectBase* fx = effectById(id);
        if (!fx) {
            continue;
        }
        if (std::find(newChain.begin(), newChain.end(), fx) != newChain.end()) {
            continue;
        }
        newChain.push_back(fx);
    }

    // Effects missing from the list re-enter at their default position.
    size_t defaultIdx = 0;
    for (const auto& [id, fx] : mEffectTable) {
        if (std::find(newChain.begin(), newChain.end(), fx) == newChain.end()) {
            const auto slot =
                static_cast<Chain::difference_type>(std::min(defaultIdx, newChain.size()));
            newChain.insert(newChain.begin() + slot, fx); // iterator offsets are signed
        }
        ++defaultIdx;
    }

    const Chain& cur = *mChain.current();
    if (newChain == cur) {
        return;
    }

    // Moved effects carry state from their old slot (delay tails, diverged
    // per-channel filters) — have the audio thread clear them.
    for (size_t i = 0; i < newChain.size(); ++i) {
        if (i >= cur.size() || cur[i] != newChain[i]) {
            newChain[i]->requestReset();
        }
    }

    // Publish a fresh vector; the one the audio thread may still be iterating
    // is retired and freed only once it has provably moved on. No waiting.
    mChain.publish(std::make_unique<Chain>(std::move(newChain)));
    reclaimRetired();
}

AudioEngine::~AudioEngine() {
    stop();
}

bool AudioEngine::start(const std::string& clientName) {
    if (mBackend && mBackend->isRunning()) {
        return true;
    }

    mBackend = IAudioBackend::create();
    if (!mBackend) {
        qCWarning(lcAudio, "[AudioEngine] No audio backend available for this platform");
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
                                   const std::string& clientName) {
    stop();

    mBackend = IAudioBackend::create();
    if (!mBackend) {
        qCWarning(lcAudio, "[AudioEngine] No audio backend available for this platform");
        return false;
    }

    if (!mBackend->startWithDevices(clientName, this, inputDeviceId, outputDeviceId)) {
        mBackend.reset();
        return false;
    }

    return true;
}

void AudioEngine::stop() {
    if (mBackend) {
        mBackend->stop();
        mBackend.reset();
    }
    // No callback can run now — drop everything retired while it was live.
    reclaimRetired();
}

std::vector<IAudioBackend::DeviceInfo> AudioEngine::enumerateInputDevices() const {
    auto temp = IAudioBackend::create();
    return temp ? temp->enumerateInputDevices() : std::vector<IAudioBackend::DeviceInfo>{};
}

std::vector<IAudioBackend::DeviceInfo> AudioEngine::enumerateOutputDevices() const {
    auto temp = IAudioBackend::create();
    return temp ? temp->enumerateOutputDevices() : std::vector<IAudioBackend::DeviceInfo>{};
}

// ---- IAudioBackend::Callback ----------------------------------------

void AudioEngine::onBufferSizeChanged(int newSize) {
    if (newSize <= 0) {
        return;
    }
    const size_t n = static_cast<size_t>(newSize); // backends report int; positive here
    if (n > mWorkBufL.size()) {
        mWorkBufL.resize(n, 0.0f);
        mWorkBufR.resize(n, 0.0f);
    }

    // Backends call this before activating the stream, and with processing
    // suspended on later changes — the one safe point to reconfigure effects.
    const double sr = mBackend ? mBackend->sampleRate() : 0.0;
    if (sr > 0.0 && sr != mChainSampleRate) {
        mChainSampleRate = sr;
        for (const auto& [id, fx] : mEffectTable) {
            fx->setSampleRate(sr);
        }
        mMetronome.setSampleRate(sr);
        mLimiter.setSampleRate(sr);
        // ~20 ms one-pole gain smoothing, independent of sample rate.
        mSmoothCoeff = std::exp(-1.0f / (0.020f * static_cast<float>(sr)));
    }

    // After SR propagation, so a re-Reset() picks up the new rate.
    mNam.prepareBuffer(std::max(n, mNam.maxBuf()));
}

void AudioEngine::onAudio(const float* in, float* outL, float* outR, size_t n) {
    // DSP load: wall time of the block vs its real-time duration. steady_clock
    // is a vDSO read — no syscall, RT-safe.
    const auto t0 = std::chrono::steady_clock::now();
    processBlock(in, outL, outR, n);
    const double secs =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (n > 0 && mChainSampleRate > 0.0) {
        const float load = static_cast<float>(secs * mChainSampleRate / static_cast<double>(n));
        if (load > mDspLoadPeak.load(std::memory_order_relaxed)) {
            mDspLoadPeak.store(load, std::memory_order_relaxed);
        }
    }
}

void AudioEngine::processBlock(const float* in, float* outL, float* outR, size_t n) {
    // Reorder fence: every exit counts the block so setChainOrder() knows
    // when a retired chain vector is safe to free.
    struct BlockGuard
    {
        RtSlot<Chain>& s;
        ~BlockGuard() {
            s.endBlock();
        }
    } guard{mChain};

    // Defensive: if a process callback ever fires before onBufferSizeChanged
    // has grown the work buffers, emit silence rather than overrunning.
    if (static_cast<size_t>(n) > mWorkBufL.size()) {
        std::fill_n(outL, n, 0.0f);
        std::fill_n(outR, n, 0.0f);
        return;
    }

    // 1. Input gain (smoothed) → both work buffers (identical at this point)
    const float targetInGain = dbToLinear(mInputGainDB.load());
    float inPeak = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        mInputGainSmooth = mSmoothCoeff * mInputGainSmooth + (1.0f - mSmoothCoeff) * targetInGain;
        float s = in[i] * mInputGainSmooth;
        mWorkBufL[i] = s;
        mWorkBufR[i] = s;
        inPeak = std::max(inPeak, std::abs(s));
    }
    // Peak-hold: accumulate the max until the GUI takes and resets it, so the
    // 50 ms poll can't miss transients between blocks.
    if (inPeak > mInputPeak.load(std::memory_order_relaxed)) {
        mInputPeak.store(inPeak, std::memory_order_relaxed);
    }

    // 2. Run the signal chain (chain vector bound once per callback; a
    //    reorder publishes a new vector and never touches this one)
    bool stereo = false;
    const Chain& chain = *mChain.acquire();
    for (auto* fx : chain) {
        fx->consumePendingReset();
        stereo = fx->process(mWorkBufL.data(), mWorkBufR.data(), n, stereo);
    }

    (void)stereo; // consumed by downstream effects; output always written to both channels

    // 3. Output gain (smoothed) + write to output buffers
    const float targetOutGain = dbToLinear(mOutputGainDB.load());
    for (size_t i = 0; i < n; ++i) {
        mOutputGainSmooth =
            mSmoothCoeff * mOutputGainSmooth + (1.0f - mSmoothCoeff) * targetOutGain;
        outL[i] = mWorkBufL[i] * mOutputGainSmooth;
        outR[i] = mWorkBufR[i] * mOutputGainSmooth;
    }

    // 4. Output limiter (look-ahead; a no-op when off), then the meter, so
    //    it shows what actually leaves the app.
    mLimiter.process(outL, outR, n);
    float outPeak = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        outPeak = std::max(outPeak, std::max(std::abs(outL[i]), std::abs(outR[i])));
    }
    if (outPeak > mOutputPeak.load(std::memory_order_relaxed)) {
        mOutputPeak.store(outPeak, std::memory_order_relaxed);
    }

    // 5. Metronome on top: own volume/pan, after the meter so the VU shows
    //    the guitar only.
    mMetronome.process(outL, outR, n);
}
