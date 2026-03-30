#pragma once

#include <atomic>
#include <array>
#include <string>

/// Chromatic tuner using autocorrelation pitch detection.
/// Buffers input samples (audio thread) and computes pitch (GUI thread).
/// Always passes audio through unmodified (or muted when mute is enabled).
class TunerProcessor
{
public:
    static constexpr int kBufSize = 4096;

    TunerProcessor();

    void setSampleRate(double sr) { mSampleRate = sr; }
    void reset();

    // Called from audio thread — accumulates samples, optionally mutes
    void processBlock(float* buf, int nframes, bool mute);

    // Called from GUI thread — runs pitch detection, returns detected frequency
    // Returns 0.0 if not enough data or no pitch detected
    float detectPitch();

    // Converts frequency to MIDI note number (A4=69=440Hz)
    static int  freqToMidi(float freq);
    // Returns note name string, e.g. "A4", "C#3"
    static std::string midiToName(int midiNote);
    // Returns cents offset from nearest semitone (-50 to +50)
    static float centsOffset(float freq);

private:
    double mSampleRate{44100.0};

    // Ring buffer filled by audio thread, consumed by GUI thread
    std::array<float, kBufSize> mRingBuf{};
    std::atomic<int>            mWritePos{0};
    std::atomic<bool>           mBufferReady{false};

    // Snapshot used by detectPitch()
    std::array<float, kBufSize> mSnapBuf{};
};
