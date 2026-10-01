#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "IAudioBackend.h"
#include "RtSlot.h"

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
#include "Metronome.h"

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
    // Loads publish through RtSlots; when audio is stopped nothing counts
    // blocks, so the wrappers reclaim retired objects eagerly in that case.
    bool        loadModel(const std::string& path);
    void        unloadModel();
    bool        isModelLoaded() const               { return mNam.isModelLoaded(); }
    std::string modelPath()     const               { return mNam.modelPath(); }
    std::string modelLoadError() const              { return mNam.lastError(); }

    bool        loadIR(const std::string& path);
    void        unloadIR();
    bool        isIRLoaded()    const           { return mIR.isLoaded(); }
    std::string irPath()        const           { return mIR.filePath(); }
    std::string irLoadError()   const           { return mIR.lastError(); }

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

    /// Mixed in after the output gain — outside the chain, not metered.
    Metronome&           metronome() { return mMetronome; }

    // ---- Signal chain (processing order = UI order) ----
    // GUI-thread snapshot of the active chain. Reordering goes through
    // setChainOrder().
    std::vector<EffectBase*> chain() const { return *mChain.current(); }

    /// Stable effect IDs in default (construction) order — the persistence
    /// contract for session state and preset schema v2.
    std::vector<std::string> defaultChainIds() const;
    /// Current chain order as IDs.
    std::vector<std::string> chainIds() const;
    EffectBase* effectById(const std::string& id) const;
    std::string idForEffect(const EffectBase* fx) const;

    /// Reorder the chain RT-safely (RtSlot publish, same pattern as model
    /// swaps — never blocks the GUI). Input is sanitized: unknown/duplicate
    /// IDs are dropped and missing effects re-enter at their default
    /// position. Effects whose position changed get a pending reset,
    /// consumed by the audio thread at the top of their next block.
    void setChainOrder(const std::vector<std::string>& ids);

    // ---- Port-routing pass-through (no-ops on backends without it) ----
    using PortConnections = IAudioBackend::PortConnections;
    PortConnections currentConnections() const { return mBackend ? mBackend->currentConnections() : PortConnections{}; }
    void            applyConnections(const PortConnections& c) { if (mBackend) mBackend->applyConnections(c); }

    // ---- VU meters ----
    // The audio thread accumulates the peak (max) until the GUI takes it;
    // take-and-reset so no transient between polls is ever missed.
    float takeInputPeak()  { return mInputPeak.exchange(0.0f);  }
    float takeOutputPeak() { return mOutputPeak.exchange(0.0f); }

    // ---- Diagnostics (status bar) ----
    /// Highest audio-thread time per block since the last take, as a
    /// fraction of the block's duration (1.0 = the whole budget).
    float takeDspLoadPeak() { return mDspLoadPeak.exchange(0.0f); }
    int   xrunCount() const { return mBackend ? mBackend->xrunCount() : -1; }
    void  resetXruns()      { if (mBackend) mBackend->resetXruns(); }
    int   ioLatencyFrames() const { return mBackend ? mBackend->ioLatencyFrames() : -1; }

    // ---- IAudioBackend::Callback ----
    void onAudio(const float* in, float* outL, float* outR, int nframes) override;
    void onBufferSizeChanged(int newSize) override;

private:
    std::unique_ptr<IAudioBackend> mBackend;

    // Signal chain — published through an RtSlot. The audio thread binds
    // one vector per callback; setChainOrder() publishes a new one and the
    // old vector is freed only after the audio thread has provably left it.
    using Chain = std::vector<EffectBase*>;
    RtSlot<Chain> mChain;

    /// Free objects retired by loads/reorders while the audio thread is idle.
    void reclaimRetired();

    // id → effect, in default chain order
    std::vector<std::pair<std::string, EffectBase*>> mEffectTable;

    // Effect instances (owned here, pointers in the chain buffers)
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
    Metronome           mMetronome;

    // Gain
    std::atomic<float> mInputGainDB {0.0f};
    std::atomic<float> mOutputGainDB{0.0f};

    float mInputGainSmooth  = 1.0f;
    float mOutputGainSmooth = 1.0f;

    std::atomic<float> mInputPeak {0.0f};
    std::atomic<float> mOutputPeak{0.0f};
    std::atomic<float> mDspLoadPeak{0.0f};

    // onAudio = timing around this.
    void processBlock(const float* in, float* outL, float* outR, int nframes);

    std::vector<float> mWorkBufL;
    std::vector<float> mWorkBufR;

    // Set from onBufferSizeChanged (never concurrent with the audio thread).
    double mChainSampleRate{0.0};
    float  mSmoothCoeff{0.999f}; // recomputed per sample rate for a ~20 ms time constant

    static inline float dbToLinear(float dB) { return std::pow(10.0f, dB / 20.0f); }
};
