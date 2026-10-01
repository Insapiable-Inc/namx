#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

/// Practice metronome (Phase 15). Not an effect: AudioEngine mixes it into
/// the output after the output gain, on its own volume and pan, so it keeps
/// running across preset loads and never goes through the chain.
///
/// RT-safety: click samples for every sound set are synthesized in
/// setSampleRate() (non-RT — called from onBufferSizeChanged); process()
/// only reads them. All settings are atomics read once per block.
///
/// Timing: BPM counts quarter notes (the same tempo the sync knobs use);
/// the meter's unit sets what a beat is (x/8 → eighths at 2 × BPM). Each beat
/// can be split into 1–4 subdivisions. A sample-accurate double accumulator
/// places every click.
class Metronome
{
public:
    static constexpr int kMaxBeats = 16;
    enum Accent : uint32_t
    {
        Mute = 0,
        Soft = 1,
        Normal = 2,
        Strong = 3
    };
    enum Sound
    {
        Click = 0,
        Wood,
        Beep,
        Cowbell,
        kSoundCount
    };

    Metronome();

    void setSampleRate(double sr); // non-RT: rebuilds the click samples

    // ---- Settings (any thread) ----
    void setRunning(bool on) {
        mRunning.store(on);
    }
    bool isRunning() const {
        return mRunning.load();
    }
    void setBpm(double bpm) {
        mBpm.store(bpm);
    }
    void setMeter(int beats, int unit);
    int beats() const {
        return mBeats.load();
    }
    int unit() const {
        return mUnit.load();
    }
    /// 2 bits per beat (Accent), beat 0 in the low bits.
    void setAccents(uint32_t packed) {
        mAccents.store(packed);
    }
    uint32_t accents() const {
        return mAccents.load();
    }
    static Accent accentOf(uint32_t packed, int beat) {
        return Accent((packed >> (2 * beat)) & 3u);
    }
    static uint32_t withAccent(uint32_t packed, int beat, Accent a) {
        return (packed & ~(3u << (2 * beat))) | (uint32_t(a) << (2 * beat));
    }
    /// Default pattern: strong downbeat; compound meters (6/8, 9/8, 12/8 …)
    /// stress each group of three; everything else Normal.
    static uint32_t defaultAccents(int beats, int unit);

    void setSubdivision(int n) {
        mSubdiv.store(n < 1 ? 1 : n > 4 ? 4 : n);
    }
    void setSound(int s) {
        mSound.store(s < 0 || s >= kSoundCount ? 0 : static_cast<size_t>(s));
    }
    void setVolumeDb(float dB) {
        mVolumeDb.store(dB);
    }
    void setPan(float pan) {
        mPan.store(pan);
    } // -1 … +1
    /// Bars counted in (all beats audible, downbeat strong) when starting.
    void setCountInBars(int bars) {
        mCountInBars.store(bars < 0 ? 0 : bars > 4 ? 4 : bars);
    }
    /// Silent after the count-in: the beat LEDs keep time, the click stops.
    void setSilentAfterCountIn(bool s) {
        mSilentAfter.store(s);
    }

    // ---- GUI polling ----
    /// Beat within the bar for the LEDs; -1 when stopped.
    int currentBeat() const {
        return mBeatNow.load(std::memory_order_relaxed);
    }
    bool inCountIn() const {
        return mCountInNow.load(std::memory_order_relaxed);
    }

    // ---- Audio thread ----
    /// Adds the click into L/R (after the output stage).
    void process(float* L, float* R, size_t n);

private:
    struct Voice
    {
        const float* data = nullptr;
        size_t len = 0;
        size_t pos = 0;
        float gain = 0.0f;
    };

    void trigger(const std::vector<float>& sample, float gain);
    void render(float* L, float* R, size_t from, size_t to, float gL, float gR);
    void buildSounds(double sr);

    // Click samples: [sound][0 = accent, 1 = beat, 2 = subdivision].
    std::array<std::array<std::vector<float>, 3>, kSoundCount> mSamples;
    double mSr = 48000.0;

    std::atomic<bool> mRunning{false};
    std::atomic<double> mBpm{120.0};
    std::atomic<int> mBeats{4};
    std::atomic<int> mUnit{4};
    std::atomic<uint32_t> mAccents;
    std::atomic<int> mSubdiv{1};
    std::atomic<size_t> mSound{Click};
    std::atomic<float> mVolumeDb{-6.0f};
    std::atomic<float> mPan{0.0f};
    std::atomic<int> mCountInBars{0};
    std::atomic<bool> mSilentAfter{false};

    std::atomic<int> mBeatNow{-1};
    std::atomic<bool> mCountInNow{false};

    // Audio-thread state.
    bool mWasRunning = false;
    double mUntilNext = 0.0; // samples from block start to the next tick
    int mTick = 0; // subdivision index within the beat
    int mBeat = 0; // beat within the bar
    int mCountInLeft = 0; // bars of count-in still to play
    std::array<Voice, 2> mVoices{};
};
