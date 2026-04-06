#pragma once

#include <atomic>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================
// Tuner  (autocorrelation-based, mono, display-only)
// ============================================================
class TunerProcessor
{
public:
    static constexpr int kBufSize = 4096;

    void setSampleRate(double sr) { mSR = sr; }

    // Returns detected frequency in Hz, or 0 if no pitch found.
    float process(float in);

    float detectedHz() const { return mDetectedHz.load(); }

private:
    double mSR = 44100.0;
    float  mBuf[kBufSize]{};
    int    mPos = 0;
    std::atomic<float> mDetectedHz{0.0f};

    float autocorrelate();
};

// ============================================================
// Noise Gate
// ============================================================
class NoiseGateProcessor
{
public:
    void setSampleRate(double sr);
    void setThreshold(float dB)   { mThreshLin = dbToLin(dB); }
    void setAttack(float ms)      { mAttackMs = ms;   updateCoeffs(); }
    void setRelease(float ms)     { mReleaseMs = ms;  updateCoeffs(); }
    void setHold(float ms)        { mHoldMs = ms; }

    float processSample(float in);

private:
    static float dbToLin(float dB) { return std::pow(10.f, dB / 20.f); }
    void updateCoeffs();

    double mSR        = 44100.0;
    float  mThreshLin = dbToLin(-60.f);
    float  mAttackMs  = 1.f;
    float  mReleaseMs = 50.f;
    float  mHoldMs    = 100.f;

    float mEnvelope   = 0.f;
    float mGain       = 0.f;
    float mAttackCoef = 0.f;
    float mReleaseCoef= 0.f;
    int   mHoldCount  = 0;
    int   mHoldSamples= 0;
    bool  mOpen       = false;
};

// ============================================================
// Compressor  (RMS, soft knee, stereo-linked)
// ============================================================
class CompressorProcessor
{
public:
    void setSampleRate(double sr);
    void setThreshold(float dB) { mThreshDB = dB; }
    void setRatio(float r)      { mRatio = r; }
    void setAttack(float ms)    { mAttackMs = ms;  updateCoeffs(); }
    void setRelease(float ms)   { mReleaseMs = ms; updateCoeffs(); }
    void setMakeupGain(float dB){ mMakeupLin = std::pow(10.f, dB / 20.f); }

    float processSample(float in);

private:
    void updateCoeffs();
    static float linToDb(float x) { return 20.f * std::log10(std::max(x, 1e-9f)); }

    double mSR         = 44100.0;
    float  mThreshDB   = -20.f;
    float  mRatio      = 4.f;
    float  mAttackMs   = 10.f;
    float  mReleaseMs  = 100.f;
    float  mMakeupLin  = 1.f;

    float  mEnvDB      = -100.f;
    float  mAttCoef    = 0.f;
    float  mRelCoef    = 0.f;
};

// ============================================================
// Chorus
// ============================================================
class ChorusProcessor
{
public:
    static constexpr int kMaxDelay = 8192;

    void setSampleRate(double sr);
    void setRate(float hz)   { mRateHz = hz; }
    void setDepth(float ms)  { mDepthMs = ms; }
    void setMix(float wet)   { mWet = wet; }

    // stereo: outL/outR written; inR may equal inL for mono source
    void processStereo(float inL, float inR, float& outL, float& outR);
    float processMono(float in);

private:
    double mSR      = 44100.0;
    float  mRateHz  = 1.0f;
    float  mDepthMs = 2.0f;
    float  mWet     = 0.5f;

    float  mPhaseL  = 0.f;
    float  mPhaseR  = static_cast<float>(M_PI / 2.0); // 90° offset for stereo width

    float  mBufL[kMaxDelay]{};
    float  mBufR[kMaxDelay]{};
    int    mWriteL  = 0;
    int    mWriteR  = 0;

    float readInterp(const float* buf, int writePos, float delaySamples) const;
};

// ============================================================
// Flanger
// ============================================================
class FlangerProcessor
{
public:
    static constexpr int kMaxDelay = 2048;

    void setSampleRate(double sr);
    void setRate(float hz)      { mRateHz = hz; }
    void setDepth(float ms)     { mDepthMs = ms; }
    void setFeedback(float fb)  { mFeedback = fb; }
    void setMix(float wet)      { mWet = wet; }

    void processStereo(float inL, float inR, float& outL, float& outR);
    float processMono(float in);

private:
    double mSR       = 44100.0;
    float  mRateHz   = 0.5f;
    float  mDepthMs  = 1.5f;
    float  mFeedback = 0.5f;
    float  mWet      = 0.5f;

    float  mPhaseL = 0.f;
    float  mPhaseR = static_cast<float>(M_PI / 2.0);

    float  mBufL[kMaxDelay]{};
    float  mBufR[kMaxDelay]{};
    int    mWriteL = 0;
    int    mWriteR = 0;

    float readInterp(const float* buf, int writePos, float delaySamples) const;
};

// ============================================================
// Phaser  (4-stage all-pass)
// ============================================================
class PhaserProcessor
{
public:
    static constexpr int kStages = 4;

    void setSampleRate(double sr);
    void setRate(float hz)      { mRateHz = hz; }
    void setDepth(float d)      { mDepth = d; }
    void setFeedback(float fb)  { mFeedback = fb; }
    void setMix(float wet)      { mWet = wet; }

    void processStereo(float inL, float inR, float& outL, float& outR);
    float processMono(float in);

private:
    double mSR       = 44100.0;
    float  mRateHz   = 0.5f;
    float  mDepth    = 1.0f;
    float  mFeedback = 0.5f;
    float  mWet      = 0.5f;

    float  mPhaseL = 0.f;
    float  mPhaseR = static_cast<float>(M_PI / 2.0);

    float  mZL[kStages]{};
    float  mZR[kStages]{};

    float allpass(float in, float coeff, float& z);
};

// ============================================================
// Tremolo
// ============================================================
class TremoloProcessor
{
public:
    void setSampleRate(double sr) { mSR = sr; }
    void setRate(float hz)   { mRateHz = hz; }
    void setDepth(float d)   { mDepth = d; }   // 0-1

    void processStereo(float inL, float inR, float& outL, float& outR);
    float processMono(float in);

private:
    double mSR     = 44100.0;
    float  mRateHz = 4.0f;
    float  mDepth  = 0.7f;
    float  mPhaseL = 0.f;
    float  mPhaseR = static_cast<float>(M_PI); // anti-phase for stereo
};

// ============================================================
// Delay  (stereo ping-pong capable)
// ============================================================
class DelayProcessor
{
public:
    static constexpr int kMaxDelaySamples = 192000; // up to ~4s at 48k

    DelayProcessor();

    void setSampleRate(double sr);
    void setTimeMs(float ms);
    void setFeedback(float fb) { mFeedback = fb; }
    void setMix(float wet)     { mWet = wet; }
    void setPingPong(bool pp)  { mPingPong = pp; }

    void processStereo(float inL, float inR, float& outL, float& outR);
    float processMono(float in);

private:
    double mSR       = 44100.0;
    float  mTimeMs   = 250.f;
    float  mFeedback = 0.4f;
    float  mWet      = 0.4f;
    bool   mPingPong = false;
    int    mDelaySamples = 0;

    std::vector<float> mBufL;
    std::vector<float> mBufR;
    int  mWriteL = 0;
    int  mWriteR = 0;
};

// ============================================================
// Reverb  (Schroeder/Freeverb-inspired, stereo)
// ============================================================
class ReverbProcessor
{
public:
    ReverbProcessor();

    void setSampleRate(double sr);
    void setRoomSize(float s)   { mRoomSize = s;    updateParams(); }
    void setDamping(float d)    { mDamping  = d;    updateParams(); }
    void setMix(float wet)      { mWet      = wet; }
    void setPredelay(float ms)  { mPredelayMs = ms; updatePredelay(); }

    void processStereo(float inL, float inR, float& outL, float& outR);
    float processMono(float in);

private:
    static constexpr int kNumCombs  = 8;
    static constexpr int kNumAllPass= 4;

    struct CombFilter
    {
        std::vector<float> buf;
        int  pos    = 0;
        float feedback = 0.f;
        float damp1    = 0.f;
        float damp2    = 0.f;
        float store    = 0.f;

        void resize(int n) { buf.assign(n, 0.f); pos = 0; store = 0; }
        float process(float in)
        {
            float out = buf[pos];
            store = out * (1.f - damp1) + store * damp1;
            buf[pos] = in + store * feedback;
            if (++pos >= (int)buf.size()) pos = 0;
            return out;
        }
    };

    struct AllPassFilter
    {
        std::vector<float> buf;
        int   pos      = 0;
        float feedback = 0.f;

        void resize(int n) { buf.assign(n, 0.f); pos = 0; }
        float process(float in)
        {
            float bufout = buf[pos];
            float out    = bufout - in;
            buf[pos]     = in + bufout * feedback;
            if (++pos >= (int)buf.size()) pos = 0;
            return out;
        }
    };

    double mSR        = 44100.0;
    float  mRoomSize  = 0.5f;
    float  mDamping   = 0.5f;
    float  mWet       = 0.3f;
    float  mPredelayMs= 0.f;

    CombFilter   mCombL[kNumCombs];
    CombFilter   mCombR[kNumCombs];
    AllPassFilter mAPL[kNumAllPass];
    AllPassFilter mAPR[kNumAllPass];

    std::vector<float> mPreBufL;
    std::vector<float> mPreBufR;
    int mPreBufPos = 0;
    int mPredelaySamples = 0;

    void updateParams();
    void updatePredelay();
    void initFilters();
};
