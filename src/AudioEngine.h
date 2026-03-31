#pragma once

#include <array>
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
#include "TunerProcessor.h"
#include "NoiseGateProcessor.h"
#include "CompressorProcessor.h"
#include "PitchShiftProcessor.h"
#include "ChorusProcessor.h"
#include "FlangerProcessor.h"
#include "PhaserProcessor.h"
#include "TremoloProcessor.h"
#include "DelayProcessor.h"
#include "ReverbProcessor.h"

/// Effect indices for the pre-NAM and post-NAM chains.
/// The processing order of both lists is dynamic (see setEffectOrder).
namespace EffectId
{
    // Pre-NAM (mono)
    enum PreNam : int { Tuner=0, Gate, Comp, PitchShift, Chorus1, Flanger, Phaser, PreNamCount };
    // Post-NAM (stereo)
    enum PostNam : int { Chorus2=0, Tremolo, Delay, Reverb, IR, PostNamCount };
}

/// Holds the processing order for both chains (double-buffered, lock-free).
struct EffectOrder
{
    std::array<int, EffectId::PreNamCount>  pre  = {0, 1, 2, 3, 4, 5, 6};
    std::array<int, EffectId::PostNamCount> post = {0, 1, 2, 3, 4};
};

class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    bool start(const std::string& clientName = "NAMPlayer");
    void stop();

    bool   isRunning()  const { return mRunning.load(); }
    double sampleRate() const { return mSampleRate; }

    // ---- Model ----
    bool        loadModel(const std::string& path);
    void        unloadModel();
    bool        isModelLoaded() const { return mModelLoaded.load(); }
    std::string modelPath()     const { return mModelPath; }

    // ---- IR ----
    bool        loadIR(const std::string& path);
    void        unloadIR();
    bool        isIRLoaded()    const;
    std::string irPath()        const;

    // ---- Gain ----
    void  setInputGain(float dB)  { mInputGainDB.store(dB);  }
    void  setOutputGain(float dB) { mOutputGainDB.store(dB); }
    float inputGain()  const { return mInputGainDB.load();  }
    float outputGain() const { return mOutputGainDB.load(); }

    // ---- EQ ----
    void  setLowGain(float dB);
    void  setMidGain(float dB);
    void  setTrebleGain(float dB);
    float lowGain()    const { return mLowDB.load();    }
    float midGain()    const { return mMidDB.load();    }
    float trebleGain() const { return mTrebleDB.load(); }

    // ---- Effect enable flags ----
    void setIREnabled(bool en)       { mIREnabled.store(en);       }
    void setTunerEnabled(bool en)    { mTunerEnabled.store(en);    }
    void setTunerMute(bool en)       { mTunerMute.store(en);       }
    void setGateEnabled(bool en)     { mGateEnabled.store(en);     }
    void setCompEnabled(bool en)     { mCompEnabled.store(en);     }
    void setPitchEnabled(bool en)    { mPitchEnabled.store(en);    }
    void setChorus1Enabled(bool en)  { mChorus1Enabled.store(en);  }
    void setFlangerEnabled(bool en)  { mFlangerEnabled.store(en);  }
    void setPhaserEnabled(bool en)   { mPhaserEnabled.store(en);   }
    void setChorus2Enabled(bool en)  { mChorus2Enabled.store(en);  }
    void setTremoloEnabled(bool en)  { mTremoloEnabled.store(en);  }
    void setDelayEnabled(bool en)    { mDelayEnabled.store(en);    }
    void setReverbEnabled(bool en)   { mReverbEnabled.store(en);   }

    bool irEnabled()      const { return mIREnabled.load();      }
    bool tunerEnabled()   const { return mTunerEnabled.load();   }
    bool tunerMute()      const { return mTunerMute.load();      }
    bool gateEnabled()    const { return mGateEnabled.load();    }
    bool compEnabled()    const { return mCompEnabled.load();    }
    bool pitchEnabled()   const { return mPitchEnabled.load();   }
    bool chorus1Enabled() const { return mChorus1Enabled.load(); }
    bool flangerEnabled() const { return mFlangerEnabled.load(); }
    bool phaserEnabled()  const { return mPhaserEnabled.load();  }
    bool chorus2Enabled() const { return mChorus2Enabled.load(); }
    bool tremoloEnabled() const { return mTremoloEnabled.load(); }
    bool delayEnabled()   const { return mDelayEnabled.load();   }
    bool reverbEnabled()  const { return mReverbEnabled.load();  }

    // ---- Effect parameter access (by reference for direct setter calls) ----
    TunerProcessor&      tuner()   { return mTuner;   }
    NoiseGateProcessor&  gate()    { return mGate;    }
    CompressorProcessor& comp()    { return mComp;    }
    PitchShiftProcessor& pitch()   { return mPitch;   }
    ChorusProcessor&     chorus1() { return mChorus1; }
    FlangerProcessor&    flanger() { return mFlanger; }
    PhaserProcessor&     phaser()  { return mPhaser;  }
    ChorusProcessor&     chorus2() { return mChorus2; }
    TremoloProcessor&    tremolo() { return mTremolo; }
    DelayProcessor&      delay()   { return mDelay;   }
    ReverbProcessor&     reverb()  { return mReverb;  }

    // ---- Dynamic effect ordering (lock-free double-buffer) ----
    /// Set both pre-NAM and post-NAM processing order atomically.
    void setEffectOrder(const EffectOrder& order);
    EffectOrder currentOrder() const;

    // ---- VU meters ----
    float inputPeak()  const { return mInputPeak.load();  }
    float outputPeak() const { return mOutputPeak.load(); }

private:
    static int  jackProcessCallback(jack_nframes_t nframes, void* arg);
    int         processAudio(jack_nframes_t nframes);
    static void jackShutdownCallback(void* arg);

    void processPreNam (int id, float* buf, int n);
    void processPostNam(int id, float* L, float* R, int n);

    jack_client_t* mClient   = nullptr;
    jack_port_t*   mPortIn   = nullptr;
    jack_port_t*   mPortOutL = nullptr;
    jack_port_t*   mPortOutR = nullptr;

    std::atomic<bool> mRunning{false};
    double            mSampleRate = 44100.0;

    // Double-buffered model swap
    std::unique_ptr<nam::DSP> mModelA, mModelB;
    std::atomic<int>          mActiveModel{0};
    std::mutex                mModelMutex;
    std::atomic<bool>         mModelLoaded{false};
    std::string               mModelPath;

    nam::DSP* activeModel()
    {
        return mActiveModel.load() == 0 ? mModelA.get() : mModelB.get();
    }

    // Effect processors
    TunerProcessor      mTuner;
    NoiseGateProcessor  mGate;
    CompressorProcessor mComp;
    PitchShiftProcessor mPitch;
    ChorusProcessor     mChorus1;
    FlangerProcessor    mFlanger;
    PhaserProcessor     mPhaser;
    EQProcessor         mEQ;
    std::mutex          mEQMutex;
    ChorusProcessor     mChorus2;
    TremoloProcessor    mTremolo;
    DelayProcessor      mDelay;
    ReverbProcessor     mReverb;
    IRLoader            mIRLoader;

    // Effect enable atomics
    std::atomic<bool> mIREnabled     {true};
    std::atomic<bool> mTunerEnabled  {false};
    std::atomic<bool> mTunerMute     {false};
    std::atomic<bool> mGateEnabled   {false};
    std::atomic<bool> mCompEnabled   {false};
    std::atomic<bool> mPitchEnabled  {false};
    std::atomic<bool> mChorus1Enabled{false};
    std::atomic<bool> mFlangerEnabled{false};
    std::atomic<bool> mPhaserEnabled {false};
    std::atomic<bool> mChorus2Enabled{false};
    std::atomic<bool> mTremoloEnabled{false};
    std::atomic<bool> mDelayEnabled  {false};
    std::atomic<bool> mReverbEnabled {false};

    // Gain
    std::atomic<float> mInputGainDB {0.0f};
    std::atomic<float> mOutputGainDB{0.0f};
    std::atomic<float> mLowDB       {0.0f};
    std::atomic<float> mMidDB       {0.0f};
    std::atomic<float> mTrebleDB    {0.0f};

    // Smoothed gain states (audio thread only)
    float mInputGainSmooth  = 1.0f;
    float mOutputGainSmooth = 1.0f;

    std::atomic<float> mInputPeak {0.0f};
    std::atomic<float> mOutputPeak{0.0f};

    // Work buffers
    std::vector<float> mWorkBuf;
    std::vector<float> mWorkBufL;
    std::vector<float> mWorkBufR;

    // Lock-free double-buffered effect order
    EffectOrder        mOrders[2];
    std::atomic<int>   mOrderBuf{0};
    std::mutex         mOrderMutex; // guards concurrent GUI writes only

    static constexpr float kSmoothCoeff = 0.999f;
    static inline float dbToLinear(float dB) { return std::pow(10.0f, dB / 20.0f); }
};
