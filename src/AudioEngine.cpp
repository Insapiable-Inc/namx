#include "AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "NAM/get_dsp.h"

AudioEngine::AudioEngine() {}
AudioEngine::~AudioEngine() { stop(); }

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
    mEQ.setSampleRate(mSampleRate);
    mTuner  .setSampleRate(mSampleRate);
    mGate   .setSampleRate(mSampleRate);
    mComp   .setSampleRate(mSampleRate);
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
        if (cur == 0) { mModelB = std::move(newModel); mActiveModel.store(1); mModelA.reset(); }
        else          { mModelA = std::move(newModel); mActiveModel.store(0); mModelB.reset(); }
        mModelPath = path;
        mModelLoaded.store(true);
    }
    return true;
}

void AudioEngine::unloadModel()
{
    std::lock_guard<std::mutex> lock(mModelMutex);
    mModelLoaded.store(false);
    mModelA.reset();
    mModelB.reset();
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
    // 2. Tuner (analyze + optional mute) — pass-through
    // ----------------------------------------------------------------
    if (mTunerEnabled.load())
        mTuner.processBlock(mWorkBuf.data(), n, mTunerMute.load());

    // ----------------------------------------------------------------
    // 3. Noise gate
    // ----------------------------------------------------------------
    if (mGateEnabled.load())
        mGate.processBlock(mWorkBuf.data(), n);

    // ----------------------------------------------------------------
    // 4. Compressor
    // ----------------------------------------------------------------
    if (mCompEnabled.load())
        mComp.processBlock(mWorkBuf.data(), n);

    // ----------------------------------------------------------------
    // 5. Chorus 1 (pre-NAM, mono)
    // ----------------------------------------------------------------
    if (mChorus1Enabled.load())
        mChorus1.processBlock(mWorkBuf.data(), n);

    // ----------------------------------------------------------------
    // 6. Flanger (mono)
    // ----------------------------------------------------------------
    if (mFlangerEnabled.load())
        mFlanger.processBlock(mWorkBuf.data(), n);

    // ----------------------------------------------------------------
    // 7. Phaser (mono)
    // ----------------------------------------------------------------
    if (mPhaserEnabled.load())
        mPhaser.processBlock(mWorkBuf.data(), n);

    // ----------------------------------------------------------------
    // 8. NAM DSP (mono → writes to outL temporarily)
    // ----------------------------------------------------------------
    nam::DSP* model = activeModel();
    if (mModelLoaded.load() && model)
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
    // 9. 3-band EQ (mono, try_lock)
    // ----------------------------------------------------------------
    if (mEQMutex.try_lock())
    {
        for (int i = 0; i < n; ++i)
            mWorkBuf[i] = mEQ.processSample(mWorkBuf[i]);
        mEQMutex.unlock();
    }

    // ----------------------------------------------------------------
    // 10. Widen to stereo — L = R = mono signal
    // ----------------------------------------------------------------
    std::copy(mWorkBuf.begin(), mWorkBuf.begin() + n, mWorkBufL.begin());
    std::copy(mWorkBuf.begin(), mWorkBuf.begin() + n, mWorkBufR.begin());

    // ----------------------------------------------------------------
    // 11. Chorus 2 (post-NAM, stereo)
    // ----------------------------------------------------------------
    if (mChorus2Enabled.load())
        mChorus2.processBlockStereo(mWorkBufL.data(), mWorkBufR.data(), n);

    // ----------------------------------------------------------------
    // 12. Tremolo (stereo)
    // ----------------------------------------------------------------
    if (mTremoloEnabled.load())
        mTremolo.processBlockStereo(mWorkBufL.data(), mWorkBufR.data(), n);

    // ----------------------------------------------------------------
    // 13. Delay (stereo ping-pong)
    // ----------------------------------------------------------------
    if (mDelayEnabled.load())
        mDelay.processBlockStereo(mWorkBufL.data(), mWorkBufR.data(), n);

    // ----------------------------------------------------------------
    // 14. Reverb (stereo)
    // ----------------------------------------------------------------
    if (mReverbEnabled.load())
        mReverb.processBlockStereo(mWorkBufL.data(), mWorkBufR.data(), n);

    // ----------------------------------------------------------------
    // 15. IR convolution (cab sim, applied to both channels)
    // ----------------------------------------------------------------
    if (mIREnabled.load() && mIRLoader.isLoaded())
    {
        mIRLoader.processBlock(mWorkBufL.data(), n);
        mIRLoader.processBlock(mWorkBufR.data(), n);
    }

    // ----------------------------------------------------------------
    // 16. Output gain (smoothed) + copy to JACK buffers
    // ----------------------------------------------------------------
    const float targetOutGain = dbToLinear(mOutputGainDB.load());
    float outPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        mOutputGainSmooth = kSmoothCoeff * mOutputGainSmooth
                          + (1.0f - kSmoothCoeff) * targetOutGain;
        outL[i] = mWorkBufL[i] * mOutputGainSmooth;
        outR[i] = mWorkBufR[i] * mOutputGainSmooth;
        float peak = std::abs(outL[i]);
        if (peak > outPeak) outPeak = peak;
    }
    mOutputPeak.store(outPeak);

    return 0;
}
