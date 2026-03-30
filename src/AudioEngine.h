#pragma once

#include <atomic>
#include <cmath>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <jack/jack.h>

#include "NAM/dsp.h"
#include "EQProcessor.h"
#include "IRLoader.h"

/// Manages JACK audio I/O and routes audio through the NAM model, EQ, and IR.
/// All parameter updates (gains, model swap) are atomic / lock-free from the GUI thread.
class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    /// Start JACK client. Returns false on failure.
    bool start(const std::string& clientName = "NAMPlayer");
    void stop();

    bool isRunning() const { return mRunning.load(); }
    double sampleRate() const { return mSampleRate; }

    // ---- Model management (called from GUI thread) ----

    /// Load a new NAM model. Swaps atomically; old model is destroyed after swap.
    bool loadModel(const std::string& path);
    void unloadModel();
    bool isModelLoaded() const { return mModelLoaded.load(); }
    std::string modelPath() const { return mModelPath; }

    // ---- IR management ----
    bool loadIR(const std::string& path);
    void unloadIR();
    bool isIRLoaded() const;
    std::string irPath() const;

    // ---- Parameters (all thread-safe via atomics) ----
    void setInputGain(float dB);
    void setOutputGain(float dB);
    void setLowGain(float dB);
    void setMidGain(float dB);
    void setTrebleGain(float dB);
    void setIREnabled(bool enabled) { mIREnabled.store(enabled); }

    float inputGain()  const { return mInputGainDB.load(); }
    float outputGain() const { return mOutputGainDB.load(); }
    float lowGain()    const { return mLowDB.load(); }
    float midGain()    const { return mMidDB.load(); }
    float trebleGain() const { return mTrebleDB.load(); }

    // ---- VU meters (peak, updated every process cycle) ----
    float inputPeak()  const { return mInputPeak.load(); }
    float outputPeak() const { return mOutputPeak.load(); }

private:
    static int jackProcessCallback(jack_nframes_t nframes, void* arg);
    int processAudio(jack_nframes_t nframes);

    static void jackShutdownCallback(void* arg);

    jack_client_t* mClient  = nullptr;
    jack_port_t*   mPortIn  = nullptr;
    jack_port_t*   mPortOut = nullptr;

    std::atomic<bool> mRunning{false};
    double            mSampleRate = 44100.0;

    // Double-buffered model swap
    std::unique_ptr<nam::DSP> mModelA;
    std::unique_ptr<nam::DSP> mModelB;
    std::atomic<int>          mActiveModel{0}; // 0=A, 1=B
    std::mutex                mModelMutex;
    std::atomic<bool>         mModelLoaded{false};
    std::string               mModelPath;

    nam::DSP* activeModel()
    {
        return mActiveModel.load() == 0 ? mModelA.get() : mModelB.get();
    }

    IRLoader    mIRLoader;
    EQProcessor mEQ;
    std::mutex  mEQMutex;

    std::atomic<bool>  mIREnabled{true};

    std::atomic<float> mInputGainDB{0.0f};
    std::atomic<float> mOutputGainDB{0.0f};
    std::atomic<float> mLowDB{0.0f};
    std::atomic<float> mMidDB{0.0f};
    std::atomic<float> mTrebleDB{0.0f};

    // Smoothed gain values (updated in audio thread)
    float mInputGainSmooth  = 1.0f;
    float mOutputGainSmooth = 1.0f;

    std::atomic<float> mInputPeak{0.0f};
    std::atomic<float> mOutputPeak{0.0f};

    // Work buffers
    std::vector<float> mWorkBuf;
    static constexpr float kSmoothCoeff = 0.999f; // per-sample smoothing pole

    static inline float dbToLinear(float dB) { return std::pow(10.0f, dB / 20.0f); }
};
