#pragma once

#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "IAudioBackend.h"

#include "NamProcessor.h"
#include "IRProcessor.h"
#include "EQProcessor.h"
#include "TunerProcessor.h"
#include "NoiseGateProcessor.h"
#include "CompressorProcessor.h"
#include "ChorusProcessor.h"
#include "FlangerProcessor.h"
#include "PhaserProcessor.h"
#include "TremoloProcessor.h"
#include "DelayProcessor.h"
#include "ReverbProcessor.h"
#include "DynamicWahProcessor.h"

/// Platform-agnostic audio engine. Owns the signal chain and all DSP
/// processors. Delegates I/O to an IAudioBackend (JACK on Linux, WASAPI on
/// Windows) created via IAudioBackend::create().
class AudioEngine : public IAudioBackend::Callback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    bool start(const std::string& clientName = "NAMPlayer");
    void stop();

    bool startWithDevices(const std::string& inputDeviceId,
                          const std::string& outputDeviceId,
                          const std::string& clientName = "NAMPlayer");

    bool   isRunning()  const { return mBackend && mBackend->isRunning(); }
    double sampleRate() const { return mBackend ? mBackend->sampleRate() : 44100.0; }
    int    bufferSize() const { return mBackend ? mBackend->bufferSize() : 0; }

    /// Human-readable status, e.g. "JACK ● 48000 Hz ● stereo out".
    std::string backendStatus()       const { return mBackend ? mBackend->statusString() : std::string{}; }
    bool        supportsPortRouting() const { return mBackend && mBackend->supportsPortRouting(); }

    // ---- Device enumeration (creates temp backend, so works before start()) ----
    std::vector<IAudioBackend::DeviceInfo> enumerateInputDevices()  const;
    std::vector<IAudioBackend::DeviceInfo> enumerateOutputDevices() const;

    // ---- Model / IR (thin wrappers for MainWindow convenience) ----
    bool        loadModel(const std::string& path) { return mNam.loadModel(path); }
    void        unloadModel()                       { mNam.unloadModel(); }
    bool        isModelLoaded() const               { return mNam.isModelLoaded(); }
    std::string modelPath()     const               { return mNam.modelPath(); }

    bool        loadIR(const std::string& path) { return mIR.load(path); }
    void        unloadIR()                      { mIR.unload(); }
    bool        isIRLoaded()    const           { return mIR.isLoaded(); }
    std::string irPath()        const           { return mIR.filePath(); }

    // ---- Gain ----
    void  setInputGain(float dB)  { mInputGainDB.store(dB);  }
    void  setOutputGain(float dB) { mOutputGainDB.store(dB); }
    float inputGain()  const { return mInputGainDB.load();  }
    float outputGain() const { return mOutputGainDB.load(); }

    // ---- Effect accessors (MainWindow calls setters directly on the effects) ----
    TunerProcessor&      tuner()   { return mTuner;   }
    NoiseGateProcessor&  gate()    { return mGate;    }
    CompressorProcessor& comp()    { return mComp;    }
    DynamicWahProcessor& wah()     { return mWah;     }
    FlangerProcessor&    flanger() { return mFlanger; }
    PhaserProcessor&     phaser()  { return mPhaser;  }
    NamProcessor&        nam()     { return mNam;     }
    EQProcessor&         eq()      { return mEQ;      }
    ChorusProcessor&     chorus()  { return mChorus;  }
    TremoloProcessor&    tremolo() { return mTremolo; }
    DelayProcessor&      delay()   { return mDelay;   }
    ReverbProcessor&     reverb()  { return mReverb;  }
    IRProcessor&         ir()      { return mIR;      }

    // ---- Signal chain (processing order = UI order) ----
    const std::vector<EffectBase*>& chain() const { return mChain; }

    // ---- Port-routing pass-through (no-ops on backends without it) ----
    using PortConnections = IAudioBackend::PortConnections;
    PortConnections currentConnections() const { return mBackend ? mBackend->currentConnections() : PortConnections{}; }
    void            applyConnections(const PortConnections& c) { if (mBackend) mBackend->applyConnections(c); }

    // ---- VU meters ----
    float inputPeak()  const { return mInputPeak.load();  }
    float outputPeak() const { return mOutputPeak.load(); }

    // ---- IAudioBackend::Callback ----
    void onAudio(const float* in, float* outL, float* outR, int nframes) override;
    void onBufferSizeChanged(int newSize) override;

private:
    std::unique_ptr<IAudioBackend> mBackend;

    // Signal chain — ordered, iterated every audio callback
    std::vector<EffectBase*> mChain;

    // Effect instances (owned here, pointers in mChain)
    TunerProcessor      mTuner;
    NoiseGateProcessor  mGate;
    CompressorProcessor mComp;
    DynamicWahProcessor mWah;
    FlangerProcessor    mFlanger;
    PhaserProcessor     mPhaser;
    NamProcessor        mNam;
    EQProcessor         mEQ;
    ChorusProcessor     mChorus;
    TremoloProcessor    mTremolo;
    DelayProcessor      mDelay;
    ReverbProcessor     mReverb;
    IRProcessor         mIR;

    // Gain
    std::atomic<float> mInputGainDB {0.0f};
    std::atomic<float> mOutputGainDB{0.0f};

    float mInputGainSmooth  = 1.0f;
    float mOutputGainSmooth = 1.0f;

    std::atomic<float> mInputPeak {0.0f};
    std::atomic<float> mOutputPeak{0.0f};

    std::vector<float> mWorkBufL;
    std::vector<float> mWorkBufR;

    static constexpr float kSmoothCoeff = 0.999f;
    static inline float dbToLinear(float dB) { return std::pow(10.0f, dB / 20.0f); }
};
