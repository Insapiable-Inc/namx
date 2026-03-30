#include "AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "NAM/get_dsp.h"

AudioEngine::AudioEngine()
{
}

AudioEngine::~AudioEngine()
{
    stop();
}

bool AudioEngine::start(const std::string& clientName)
{
    if (mRunning.load())
        return true;

    jack_status_t status;
    mClient = jack_client_open(clientName.c_str(), JackNullOption, &status);
    if (!mClient)
    {
        std::cerr << "[AudioEngine] jack_client_open failed, status=0x" << std::hex << status << "\n";
        return false;
    }

    mSampleRate = static_cast<double>(jack_get_sample_rate(mClient));
    mEQ.setSampleRate(mSampleRate);

    const int maxBuf = static_cast<int>(jack_get_buffer_size(mClient));
    mWorkBuf.resize(maxBuf, 0.0f);

    jack_set_process_callback(mClient, jackProcessCallback, this);
    jack_on_shutdown(mClient, jackShutdownCallback, this);

    mPortIn  = jack_port_register(mClient, "input",  JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput,  0);
    mPortOut = jack_port_register(mClient, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

    if (!mPortIn || !mPortOut)
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
    std::cout << "[AudioEngine] JACK started at " << mSampleRate << " Hz\n";
    return true;
}

void AudioEngine::stop()
{
    if (!mRunning.load())
        return;
    mRunning.store(false);
    if (mClient)
    {
        jack_deactivate(mClient);
        jack_client_close(mClient);
        mClient = nullptr;
    }
}

// ---- Model management ------------------------------------------------

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

    if (!newModel)
        return false;

    newModel->Reset(mSampleRate, static_cast<int>(mWorkBuf.size()));
    newModel->prewarm();

    {
        std::lock_guard<std::mutex> lock(mModelMutex);
        int current = mActiveModel.load();
        if (current == 0)
        {
            mModelB = std::move(newModel);
            mActiveModel.store(1);
            mModelA.reset(); // destroy old
        }
        else
        {
            mModelA = std::move(newModel);
            mActiveModel.store(0);
            mModelB.reset();
        }
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

// ---- IR management ---------------------------------------------------

bool AudioEngine::loadIR(const std::string& path)
{
    return mIRLoader.load(path);
}

void AudioEngine::unloadIR()
{
    mIRLoader.unload();
}

bool AudioEngine::isIRLoaded() const
{
    return mIRLoader.isLoaded();
}

std::string AudioEngine::irPath() const
{
    return mIRLoader.getFilePath();
}

// ---- Parameter setters -----------------------------------------------

void AudioEngine::setInputGain(float dB)  { mInputGainDB.store(dB); }
void AudioEngine::setOutputGain(float dB) { mOutputGainDB.store(dB); }

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
    auto* self = static_cast<AudioEngine*>(arg);
    self->mRunning.store(false);
    std::cerr << "[AudioEngine] JACK server shut down\n";
}

int AudioEngine::processAudio(jack_nframes_t nframes)
{
    auto* inBuf  = static_cast<jack_default_audio_sample_t*>(jack_port_get_buffer(mPortIn,  nframes));
    auto* outBuf = static_cast<jack_default_audio_sample_t*>(jack_port_get_buffer(mPortOut, nframes));

    const int n = static_cast<int>(nframes);

    // --- Input gain (smoothed) ---
    const float targetInGain = dbToLinear(mInputGainDB.load());
    float inPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        mInputGainSmooth = kSmoothCoeff * mInputGainSmooth + (1.0f - kSmoothCoeff) * targetInGain;
        float s = inBuf[i] * mInputGainSmooth;
        mWorkBuf[i] = s;
        inPeak = std::max(inPeak, std::abs(s));
    }
    mInputPeak.store(inPeak);

    // --- NAM model ---
    nam::DSP* model = activeModel();
    if (mModelLoaded.load() && model)
    {
        // NAM_SAMPLE == float (we defined NAM_SAMPLE_FLOAT)
        NAM_SAMPLE* inPtr  = mWorkBuf.data();
        NAM_SAMPLE* outPtr = outBuf;

        NAM_SAMPLE** inPtrs  = &inPtr;
        NAM_SAMPLE** outPtrs = &outPtr;

        model->process(inPtrs, outPtrs, n);
        // outBuf now holds the model output
    }
    else
    {
        // Bypass: copy input to output
        std::copy(mWorkBuf.begin(), mWorkBuf.begin() + n, outBuf);
    }

    // --- 3-band EQ ---
    // Try-lock to avoid blocking audio thread if GUI is updating EQ params
    if (mEQMutex.try_lock())
    {
        for (int i = 0; i < n; ++i)
            outBuf[i] = mEQ.processSample(outBuf[i]);
        mEQMutex.unlock();
    }
    // (if lock fails, just skip EQ this block — inaudible at typical block sizes)

    // --- IR convolution (cab sim) ---
    if (mIREnabled.load() && mIRLoader.isLoaded())
        mIRLoader.processBlock(outBuf, n);

    // --- Output gain (smoothed) ---
    const float targetOutGain = dbToLinear(mOutputGainDB.load());
    float outPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        mOutputGainSmooth = kSmoothCoeff * mOutputGainSmooth + (1.0f - kSmoothCoeff) * targetOutGain;
        outBuf[i] *= mOutputGainSmooth;
        outPeak = std::max(outPeak, std::abs(outBuf[i]));
    }
    mOutputPeak.store(outPeak);

    return 0;
}
