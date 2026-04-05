#include "AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "NAM/get_dsp.h"

AudioEngine::AudioEngine() {}
AudioEngine::~AudioEngine()
{
    stop(); // deactivates JACK — no more audio callbacks after this returns
    // Safe to destroy models now that the audio thread is stopped
    std::lock_guard<std::mutex> lock(mModelMutex);
    mModelA.reset();
    mModelB.reset();
}

bool AudioEngine::start(const std::string& clientName)
{
    if (mRunning.load()) return true;

    jack_status_t status;
    mClient = jack_client_open(clientName.c_str(), JackNullOption, &status);
    if (!mClient)
    {
        std::cerr << "[AudioEngine] jack_client_open failed, status=0x"
                  << std::hex << status << "\n";
        return false;
    }

    mSampleRate = static_cast<double>(jack_get_sample_rate(mClient));
    const int maxBuf = static_cast<int>(jack_get_buffer_size(mClient));

    // Allocate work buffers
    mWorkBuf .resize(maxBuf, 0.0f);
    mWorkBufL.resize(maxBuf, 0.0f);
    mWorkBufR.resize(maxBuf, 0.0f);

    // Initialise all effects with sample rate
    mEQ     .setSampleRate(mSampleRate);
    mTuner  .setSampleRate(mSampleRate);
    mGate   .setSampleRate(mSampleRate);
    mComp   .setSampleRate(mSampleRate);
    mPitch  .setSampleRate(mSampleRate);
    mChorus1.setSampleRate(mSampleRate);
    mFlanger.setSampleRate(mSampleRate);
    mPhaser .setSampleRate(mSampleRate);
    mChorus2.setSampleRate(mSampleRate);
    mTremolo.setSampleRate(mSampleRate);
    mDelay  .setSampleRate(mSampleRate);
    mReverb .setSampleRate(mSampleRate);

    jack_set_process_callback(mClient, jackProcessCallback, this);
    jack_on_shutdown(mClient, jackShutdownCallback, this);

    mPortIn   = jack_port_register(mClient, "input",    JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput,  0);
    mPortOutL = jack_port_register(mClient, "output_L", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    mPortOutR = jack_port_register(mClient, "output_R", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

    if (!mPortIn || !mPortOutL || !mPortOutR)
    {
        std::cerr << "[AudioEngine] Failed to register JACK ports\n";
        jack_client_close(mClient);
        mClient = nullptr;
        return false;
    }

    if (jack_activate(mClient) != 0)
    {
        std::cerr << "[AudioEngine] jack_activate failed\n";
        jack_client_close(mClient);
        mClient = nullptr;
        return false;
    }

    mRunning.store(true);
    std::cout << "[AudioEngine] JACK started at " << mSampleRate << " Hz (stereo out)\n";
    return true;
}

void AudioEngine::stop()
{
    if (!mRunning.load()) return;
    mRunning.store(false);
    if (mClient)
    {
        jack_deactivate(mClient);
        jack_client_close(mClient);
        mClient = nullptr;
    }
}

// ---- Model management -------------------------------------------------

bool AudioEngine::loadModel(const std::string& path)
{
    std::unique_ptr<nam::DSP> newModel;
    try
    {
        newModel = nam::get_dsp(std::filesystem::path(path));
    }
    catch (const std::exception& e)
    {
        std::cerr << "[AudioEngine] Failed to load model: " << e.what() << "\n";
        return false;
    }
    if (!newModel) return false;

    newModel->Reset(mSampleRate, static_cast<int>(mWorkBuf.size()));
    newModel->prewarm();

    {
        std::lock_guard<std::mutex> lock(mModelMutex);
        int cur = mActiveModel.load();
        if (cur == 0) {
            // Write new model into inactive slot B, then flip.
            // Old model in A stays alive until the next loadModel overwrites it,
            // ensuring the audio thread can never access a destroyed object.
            mModelB = std::move(newModel);
            mActiveModel.store(1, std::memory_order_release);
        } else {
            mModelA = std::move(newModel);
            mActiveModel.store(0, std::memory_order_release);
        }
        mModelPath = path;
        mModelLoaded.store(true);
    }
    return true;
}

void AudioEngine::unloadModel()
{
    // Clear the flag first. The audio thread may still be mid-callback with a valid
    // pointer; that callback will complete safely. After the store, the audio thread
    // won't call process() again. The unique_ptrs are destroyed in ~AudioEngine after
    // JACK is stopped, or overwritten safely on the next loadModel() call.
    mModelLoaded.store(false);
    mModelPath = "";
}

// ---- IR management ----------------------------------------------------

bool        AudioEngine::loadIR(const std::string& path) { return mIRLoader.load(path); }
void        AudioEngine::unloadIR()                       { mIRLoader.unload(); }
bool        AudioEngine::isIRLoaded() const               { return mIRLoader.isLoaded(); }
std::string AudioEngine::irPath()     const               { return mIRLoader.getFilePath(); }

// ---- EQ parameter setters --------------------------------------------

void AudioEngine::setLowGain(float dB)
{
    mLowDB.store(dB);
    std::lock_guard<std::mutex> lock(mEQMutex);
    mEQ.setLowGain(static_cast<double>(dB));
}

void AudioEngine::setMidGain(float dB)
{
    mMidDB.store(dB);
    std::lock_guard<std::mutex> lock(mEQMutex);
    mEQ.setMidGain(static_cast<double>(dB));
}

void AudioEngine::setTrebleGain(float dB)
{
    mTrebleDB.store(dB);
    std::lock_guard<std::mutex> lock(mEQMutex);
    mEQ.setTrebleGain(static_cast<double>(dB));
}

// ---- Dynamic effect order --------------------------------------------

void AudioEngine::setEffectOrder(const EffectOrder& order)
{
    std::lock_guard<std::mutex> lock(mOrderMutex);
    // Write to the inactive buffer, then flip
    int next = 1 - mOrderBuf.load(std::memory_order_relaxed);
    mOrders[next] = order;
    mOrderBuf.store(next, std::memory_order_release);
}

EffectOrder AudioEngine::currentOrder() const
{
    return mOrders[mOrderBuf.load(std::memory_order_acquire)];
}

// ---- Per-effect dispatch helpers -------------------------------------

void AudioEngine::processPreNam(int id, float* buf, int n)
{
    switch (id)
    {
        case EffectId::Tuner:
            if (mTunerEnabled.load())
                mTuner.processBlock(buf, n, mTunerMute.load());
            break;
        case EffectId::Gate:
            if (mGateEnabled.load())
                mGate.processBlock(buf, n);
            break;
        case EffectId::Comp:
            if (mCompEnabled.load())
                mComp.processBlock(buf, n);
            break;
        case EffectId::PitchShift:
            if (mPitchEnabled.load())
                mPitch.processBlock(buf, n);
            break;
        case EffectId::Chorus1:
            if (mChorus1Enabled.load() && !mChorus1Stereo.load())
                mChorus1.processBlock(buf, n);
            break;
        case EffectId::Flanger:
            if (mFlangerEnabled.load() && !mFlangerStereo.load())
                mFlanger.processBlock(buf, n);
            break;
        case EffectId::Phaser:
            if (mPhaserEnabled.load() && !mPhaserStereo.load())
                mPhaser.processBlock(buf, n);
            break;
        default: break;
    }
}

void AudioEngine::processPostNam(int id, float* L, float* R, int n)
{
    switch (id)
    {
        case EffectId::Chorus2:
            if (mChorus2Enabled.load())
                mChorus2.processBlockStereo(L, R, n);
            break;
        case EffectId::Tremolo:
            if (mTremoloEnabled.load())
                mTremolo.processBlockStereo(L, R, n);
            break;
        case EffectId::Delay:
            if (mDelayEnabled.load()) {
                if (mDelayStereo.load()) {
                    bool trueStereo = mChorus2Enabled.load() || mTremoloEnabled.load();
                    if (trueStereo)
                        mDelay.processBlockStereoIndependent(L, R, n);
                    else
                        mDelay.processBlockStereo(L, R, n);
                } else {
                    mDelay.processBlock(L, n);
                    std::copy(L, L + n, R);
                }
            }
            break;
        case EffectId::Reverb:
            if (mReverbEnabled.load())
                mReverb.processBlockStereo(L, R, n);
            break;
        case EffectId::IR:
            if (mIREnabled.load() && mIRLoader.isLoaded())
                mIRLoader.processBlockStereo(L, R, n);
            break;
        default: break;
    }
}

// ---- JACK callbacks --------------------------------------------------

int AudioEngine::jackProcessCallback(jack_nframes_t nframes, void* arg)
{
    return static_cast<AudioEngine*>(arg)->processAudio(nframes);
}

void AudioEngine::jackShutdownCallback(void* arg)
{
    static_cast<AudioEngine*>(arg)->mRunning.store(false);
    std::cerr << "[AudioEngine] JACK server shut down\n";
}

int AudioEngine::processAudio(jack_nframes_t nframes)
{
    auto* inBuf  = static_cast<float*>(jack_port_get_buffer(mPortIn,   nframes));
    auto* outL   = static_cast<float*>(jack_port_get_buffer(mPortOutL, nframes));
    auto* outR   = static_cast<float*>(jack_port_get_buffer(mPortOutR, nframes));

    const int n = static_cast<int>(nframes);

    // ----------------------------------------------------------------
    // 1. Input gain (smoothed) → mWorkBuf (mono)
    // ----------------------------------------------------------------
    const float targetInGain = dbToLinear(mInputGainDB.load());
    float inPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        mInputGainSmooth = kSmoothCoeff * mInputGainSmooth
                         + (1.0f - kSmoothCoeff) * targetInGain;
        float s = inBuf[i] * mInputGainSmooth;
        mWorkBuf[i] = s;
        if (s < 0.0f) s = -s;
        if (s > inPeak) inPeak = s;
    }
    mInputPeak.store(inPeak);

    // ----------------------------------------------------------------
    // 2. Pre-NAM effects (dynamic order)
    // ----------------------------------------------------------------
    const EffectOrder& order = mOrders[mOrderBuf.load(std::memory_order_acquire)];
    for (int id : order.pre)
        processPreNam(id, mWorkBuf.data(), n);

    // ----------------------------------------------------------------
    // 3. NAM DSP (mono → writes to outL temporarily)
    // ----------------------------------------------------------------
    nam::DSP* model = activeModel();
    if (mModelLoaded.load() && model && mNAMEnabled.load())
    {
        NAM_SAMPLE* inPtr  = mWorkBuf.data();
        NAM_SAMPLE* outPtr = outL;         // borrow outL as scratch
        NAM_SAMPLE** inPtrs  = &inPtr;
        NAM_SAMPLE** outPtrs = &outPtr;
        model->process(inPtrs, outPtrs, n);
        // Copy NAM output back to mWorkBuf
        std::copy(outL, outL + n, mWorkBuf.begin());
    }

    // ----------------------------------------------------------------
    // 4. 3-band EQ (mono, try_lock)
    // ----------------------------------------------------------------
    if (mEQEnabled.load() && mEQMutex.try_lock())
    {
        for (int i = 0; i < n; ++i)
            mWorkBuf[i] = mEQ.processSample(mWorkBuf[i]);
        mEQMutex.unlock();
    }

    // ----------------------------------------------------------------
    // 5. Widen to stereo — L = R = mono signal
    // ----------------------------------------------------------------
    std::copy(mWorkBuf.begin(), mWorkBuf.begin() + n, mWorkBufL.begin());
    std::copy(mWorkBuf.begin(), mWorkBuf.begin() + n, mWorkBufR.begin());

    // ----------------------------------------------------------------
    // 5.5. Pre-NAM effects in stereo mode (applied after L/R split)
    // ----------------------------------------------------------------
    if (mChorus1Enabled.load() && mChorus1Stereo.load())
        mChorus1.processBlockStereo(mWorkBufL.data(), mWorkBufR.data(), n);
    if (mFlangerEnabled.load() && mFlangerStereo.load())
        mFlanger.processBlockStereo(mWorkBufL.data(), mWorkBufR.data(), n);
    if (mPhaserEnabled.load() && mPhaserStereo.load())
        mPhaser.processBlockStereo(mWorkBufL.data(), mWorkBufR.data(), n);

    // ----------------------------------------------------------------
    // 6. Post-NAM effects (dynamic order)
    // ----------------------------------------------------------------
    for (int id : order.post)
        processPostNam(id, mWorkBufL.data(), mWorkBufR.data(), n);

    // ----------------------------------------------------------------
    // 7. Output gain (smoothed) + copy to JACK buffers
    // ----------------------------------------------------------------
    const float targetOutGain = dbToLinear(mOutputGainDB.load());
    float outPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        mOutputGainSmooth = kSmoothCoeff * mOutputGainSmooth
                          + (1.0f - kSmoothCoeff) * targetOutGain;
        outL[i] = mWorkBufL[i] * mOutputGainSmooth;
        outR[i] = mWorkBufR[i] * mOutputGainSmooth;
        float peak = std::max(std::abs(outL[i]), std::abs(outR[i]));
        if (peak > outPeak) outPeak = peak;
    }
    mOutputPeak.store(outPeak);

    return 0;
}
