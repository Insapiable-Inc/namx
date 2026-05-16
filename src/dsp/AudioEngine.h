#pragma once

#include <atomic>
#include <cmath>
#include <string>
#include <vector>

#include <jack/jack.h>

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

class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    bool start(const std::string& clientName = "NAMPlayer");
    void stop();

    bool   isRunning()  const { return mRunning.load(); }
    double sampleRate() const { return mSampleRate; }

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
    ChorusProcessor&     chorus1() { return mChorus1; }
    FlangerProcessor&    flanger() { return mFlanger; }
    PhaserProcessor&     phaser()  { return mPhaser;  }
    NamProcessor&        nam()     { return mNam;     }
    EQProcessor&         eq()      { return mEQ;      }
    ChorusProcessor&     chorus2() { return mChorus2; }
    TremoloProcessor&    tremolo() { return mTremolo; }
    DelayProcessor&      delay()   { return mDelay;   }
    ReverbProcessor&     reverb()  { return mReverb;  }
    IRProcessor&         ir()      { return mIR;      }

    // ---- Signal chain (processing order = UI order) ----
    const std::vector<EffectBase*>& chain() const { return mChain; }

    // ---- JACK port connection persistence ----
    struct PortConnections
    {
        std::vector<std::string> inputSources;   // ports → our input
        std::vector<std::string> outputLDests;   // our output_L → ports
        std::vector<std::string> outputRDests;   // our output_R → ports
    };
    PortConnections currentConnections() const;
    void            applyConnections(const PortConnections& c);

    // ---- VU meters ----
    float inputPeak()  const { return mInputPeak.load();  }
    float outputPeak() const { return mOutputPeak.load(); }

private:
    static int  jackProcessCallback(jack_nframes_t nframes, void* arg);
    int         processAudio(jack_nframes_t nframes);
    static void jackShutdownCallback(void* arg);

    jack_client_t* mClient   = nullptr;
    jack_port_t*   mPortIn   = nullptr;
    jack_port_t*   mPortOutL = nullptr;
    jack_port_t*   mPortOutR = nullptr;

    std::atomic<bool> mRunning{false};
    double            mSampleRate = 44100.0;

    // Signal chain — ordered, iterated every audio callback
    std::vector<EffectBase*> mChain;

    // Effect instances (owned here, pointers in mChain)
    TunerProcessor      mTuner;
    NoiseGateProcessor  mGate;
    CompressorProcessor mComp;
    DynamicWahProcessor mWah;
    ChorusProcessor     mChorus1;
    FlangerProcessor    mFlanger;
    PhaserProcessor     mPhaser;
    NamProcessor        mNam;
    EQProcessor         mEQ;
    ChorusProcessor     mChorus2;
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
