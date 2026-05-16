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
    jack_set_buffer_size_callback(mClient, jackBufferSizeCallback, this);
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

// ---- Port connection persistence ------------------------------------

AudioEngine::PortConnections AudioEngine::currentConnections() const
{
    PortConnections out;
    if (!mClient) return out;

    auto getConns = [](jack_port_t* p) {
        std::vector<std::string> result;
        if (!p) return result;
        const char** conns = jack_port_get_connections(p);
        if (!conns) return result;
        for (int i = 0; conns[i]; ++i)
            result.emplace_back(conns[i]);
        jack_free(conns);
        return result;
    };

    out.inputSources = getConns(mPortIn);
    out.outputLDests = getConns(mPortOutL);
    out.outputRDests = getConns(mPortOutR);
    return out;
}

void AudioEngine::applyConnections(const PortConnections& c)
{
    if (!mClient) return;

    // Disconnect everything currently bound to our ports, then re-apply the
    // saved set. Prevents PipeWire-JACK auto-connects from layering on top.
    auto disconnectAll = [this](jack_port_t* p) {
        if (!p) return;
        const char** conns = jack_port_get_connections(p);
        if (!conns) return;
        const char* myName  = jack_port_name(p);
        const bool  isOut   = (jack_port_flags(p) & JackPortIsOutput) != 0;
        for (int i = 0; conns[i]; ++i)
        {
            if (isOut) jack_disconnect(mClient, myName,   conns[i]);
            else       jack_disconnect(mClient, conns[i], myName);
        }
        jack_free(conns);
    };
    disconnectAll(mPortIn);
    disconnectAll(mPortOutL);
    disconnectAll(mPortOutR);

    const char* inName   = jack_port_name(mPortIn);
    const char* outLName = jack_port_name(mPortOutL);
    const char* outRName = jack_port_name(mPortOutR);

    for (const auto& src : c.inputSources) jack_connect(mClient, src.c_str(), inName);
    for (const auto& dst : c.outputLDests) jack_connect(mClient, outLName, dst.c_str());
    for (const auto& dst : c.outputRDests) jack_connect(mClient, outRName, dst.c_str());
}

// ---- JACK callbacks --------------------------------------------------

int AudioEngine::jackProcessCallback(jack_nframes_t nframes, void* arg)
{
    return static_cast<AudioEngine*>(arg)->processAudio(nframes);
}

int AudioEngine::jackBufferSizeCallback(jack_nframes_t nframes, void* arg)
{
    return static_cast<AudioEngine*>(arg)->onBufferSizeChanged(nframes);
}

// Called by JACK on a non-RT thread, serialized with processAudio.
// Grows the work buffers and NAM scratch so the next process callback never
// overflows. Only grows — shrinking would just churn the heap for no benefit.
int AudioEngine::onBufferSizeChanged(jack_nframes_t nframes)
{
    const int n = static_cast<int>(nframes);
    if (n <= 0) return 0;
    if (n > static_cast<int>(mWorkBufL.size()))
    {
        mWorkBufL.resize(n, 0.0f);
        mWorkBufR.resize(n, 0.0f);
    }
    mNam.prepareBuffer(std::max(n, mNam.maxBuf()));
    return 0;
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

    // Defensive: if a process callback ever fires before onBufferSizeChanged
    // has grown the work buffers (PipeWire-JACK quantum races), emit silence
    // for this cycle rather than overrunning mWorkBufL/R.
    if (static_cast<size_t>(n) > mWorkBufL.size())
    {
        std::fill_n(outL, n, 0.0f);
        std::fill_n(outR, n, 0.0f);
        return 0;
    }

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
