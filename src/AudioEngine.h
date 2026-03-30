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
#include "TunerProcessor.h"
#include "NoiseGateProcessor.h"
#include "CompressorProcessor.h"
#include "ChorusProcessor.h"
#include "FlangerProcessor.h"
#include "PhaserProcessor.h"
#include "TremoloProcessor.h"
#include "DelayProcessor.h"
#include "ReverbProcessor.h"

/// Manages JACK audio I/O and routes audio through the full signal chain.
/// Signal chain:
///   Input → Gain → [Tuner] → [Gate] → [Comp] → [Chorus1] → [Flanger]
///   → [Phaser] → NAM DSP → EQ → [Chorus2-stereo] → [Tremolo-stereo]
///   → [Delay-stereo] → [Reverb-stereo] → [IR-stereo] → Output gain → stereo out
class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    bool start(const std::string& clientName = "NAMPlayer");
    void stop();

    bool   isRunning()   const { return mRunning.load(); }
    double sampleRate()  const { return mSampleRate; }

    // ---- Model management ----
    bool        loadModel(const std::string& path);
    void        unloadModel();
    bool        isModelLoaded() const { return mModelLoaded.load(); }
    std::string modelPath()     const { return mModelPath; }

    // ---- IR management ----
    bool        loadIR(const std::string& path);
    void        unloadIR();
    bool        isIRLoaded()    const;
    std::string irPath()        const;

    // ---- Gain parameters ----
    void setInputGain(float dB)   { mInputGainDB.store(dB);  }
    void setOutputGain(float dB)  { mOutputGainDB.store(dB); }
    float inputGain()  const { return mInputGainDB.load();  }
    float outputGain() const { return mOutputGainDB.load(); }

    // ---- EQ ----
    void setLowGain(float dB);
    void setMidGain(float dB);
    void setTrebleGain(float dB);
    float lowGain()    const { return mLowDB.load();    }
    float midGain()    const { return mMidDB.load();    }
    float trebleGain() const { return mTrebleDB.load(); }

    // ---- Effect enable/disable ----
    void setIREnabled(bool en)       { mIREnabled.store(en);        }
    void setTunerEnabled(bool en)    { mTunerEnabled.store(en);     }
    void setTunerMute(bool en)       { mTunerMute.store(en);        }
    void setGateEnabled(bool en)     { mGateEnabled.store(en);      }
    void setCompEnabled(bool en)     { mCompEnabled.store(en);      }
    void setChorus1Enabled(bool en)  { mChorus1Enabled.store(en);   }
    void setFlangerEnabled(bool en)  { mFlangerEnabled.store(en);   }
    void setPhaserEnabled(bool en)   { mPhaserEnabled.store(en);    }
    void setChorus2Enabled(bool en)  { mChorus2Enabled.store(en);   }
    void setTremoloEnabled(bool en)  { mTremoloEnabled.store(en);   }
    void setDelayEnabled(bool en)    { mDelayEnabled.store(en);     }
    void setReverbEnabled(bool en)   { mReverbEnabled.store(en);    }

    bool irEnabled()      const { return mIREnabled.load();      }
    bool tunerEnabled()   const { return mTunerEnabled.load();   }
    bool tunerMute()      const { return mTunerMute.load();      }
    bool gateEnabled()    const { return mGateEnabled.load();    }
    bool compEnabled()    const { return mCompEnabled.load();    }
    bool chorus1Enabled() const { return mChorus1Enabled.load(); }
    bool flangerEnabled() const { return mFlangerEnabled.load(); }
    bool phaserEnabled()  const { return mPhaserEnabled.load();  }
    bool chorus2Enabled() const { return mChorus2Enabled.load(); }
    bool tremoloEnabled() const { return mTremoloEnabled.load(); }
    bool delayEnabled()   const { return mDelayEnabled.load();   }
    bool reverbEnabled()  const { return mReverbEnabled.load();  }

    // ---- Effect parameter access ----
    NoiseGateProcessor& gate()     { return mGate;     }
    CompressorProcessor& comp()    { return mComp;     }
    ChorusProcessor&  chorus1()    { return mChorus1;  }
    FlangerProcessor& flanger()    { return mFlanger;  }
    PhaserProcessor&  phaser()     { return mPhaser;   }
    ChorusProcessor&  chorus2()    { return mChorus2;  }
    TremoloProcessor& tremolo()    { return mTremolo;  }
    DelayProcessor&   delay()      { return mDelay;    }
    ReverbProcessor&  reverb()     { return mReverb;   }
    TunerProcessor&   tuner()      { return mTuner;    }

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
    TunerProcessor    mTuner;
    NoiseGateProcessor mGate;
    CompressorProcessor mComp;
    ChorusProcessor   mChorus1;
    FlangerProcessor  mFlanger;
    PhaserProcessor   mPhaser;
    EQProcessor       mEQ;
    std::mutex        mEQMutex;
    ChorusProcessor   mChorus2;
    TremoloProcessor  mTremolo;
    DelayProcessor    mDelay;
    ReverbProcessor   mReverb;
    IRLoader          mIRLoader;

    // Effect enable flags
    std::atomic<bool> mIREnabled     {true};
    std::atomic<bool> mTunerEnabled  {false};
    std::atomic<bool> mTunerMute     {false};
    std::atomic<bool> mGateEnabled   {false};
    std::atomic<bool> mCompEnabled   {false};
    std::atomic<bool> mChorus1Enabled{false};
    std::atomic<bool> mFlangerEnabled{false};
    std::atomic<bool> mPhaserEnabled {false};
    std::atomic<bool> mChorus2Enabled{false};
    std::atomic<bool> mTremoloEnabled{false};
    std::atomic<bool> mDelayEnabled  {false};
    std::atomic<bool> mReverbEnabled {false};

    // Gain atomics
    std::atomic<float> mInputGainDB {0.0f};
    std::atomic<float> mOutputGainDB{0.0f};
    std::atomic<float> mLowDB       {0.0f};
    std::atomic<float> mMidDB       {0.0f};
    std::atomic<float> mTrebleDB    {0.0f};

    // Smoothed gains (audio thread only)
    float mInputGainSmooth  = 1.0f;
    float mOutputGainSmooth = 1.0f;

    std::atomic<float> mInputPeak {0.0f};
    std::atomic<float> mOutputPeak{0.0f};

    // Work buffers (mono pre-NAM, stereo post-NAM)
    std::vector<float> mWorkBuf;  // mono scratch
    std::vector<float> mWorkBufL; // stereo L (post-NAM)
    std::vector<float> mWorkBufR; // stereo R (post-NAM)

    static constexpr float kSmoothCoeff = 0.999f;
    static inline float dbToLinear(float dB) { return std::pow(10.0f, dB / 20.0f); }
};
