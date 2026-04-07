#include "AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <iostream>

AudioEngine::AudioEngine()
{
    // Effects that are naturally stereo default to stereo mode on.
    // Chorus1/Flanger/Phaser stereo mode is user-controlled (default off).
    mChorus2.setStereoMode(true);
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
        //&mChorus1,
        &mFlanger,
        &mPhaser,
        &mNam,
        &mEQ,
        &mChorus2,
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
    if (mRunning.load()) return true;

    jack_status_t status;
    mClient = jack_client_open(clientName.c_str(), JackNullOption, &status);
    if (!mClient)
    {
        std::cerr << "[AudioEngine] jack_client_open failed, status=0x"
                  << std::hex << status << "\n";
        return false;
    }

    mSampleRate        = static_cast<double>(jack_get_sample_rate(mClient));
    const int maxBuf   = static_cast<int>(jack_get_buffer_size(mClient));

    mWorkBufL.resize(maxBuf, 0.0f);
    mWorkBufR.resize(maxBuf, 0.0f);

    // Propagate sample rate to all effects in the chain.
    for (auto* fx : mChain)
        fx->setSampleRate(mSampleRate);

    // NAM needs the buffer size for model Reset().
    mNam.prepareBuffer(maxBuf);

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
    auto* inBuf = static_cast<float*>(jack_port_get_buffer(mPortIn,   nframes));
    auto* outL  = static_cast<float*>(jack_port_get_buffer(mPortOutL, nframes));
    auto* outR  = static_cast<float*>(jack_port_get_buffer(mPortOutR, nframes));

    const int n = static_cast<int>(nframes);

    // 1. Input gain (smoothed) → both work buffers (identical at this point)
    const float targetInGain = dbToLinear(mInputGainDB.load());
    float inPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        mInputGainSmooth = kSmoothCoeff * mInputGainSmooth
                         + (1.0f - kSmoothCoeff) * targetInGain;
        float s = inBuf[i] * mInputGainSmooth;
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

    // 3. Output gain (smoothed) + write to JACK buffers
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

    return 0;
}
