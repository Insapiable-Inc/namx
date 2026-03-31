#pragma once

#include <array>
#include <mutex>
#include <string>

/// Chromatic tuner using NSDF (YIN-style) pitch detection.
/// Buffers input samples (audio thread) and computes pitch (GUI thread).
/// Always passes audio through unmodified (or muted when mute is enabled).
class TunerProcessor
{
public:
    static constexpr int kBufSize = 4096;

    TunerProcessor();

    void setSampleRate(double sr) { mSampleRate = sr; }
    void reset();

    // Called from audio thread — accumulates samples, optionally mutes.
    // Uses try_lock: if the GUI is snapshotting, the write is skipped for this block
    // (a ~5ms gap in tuner data is completely inaudible for a tuner display).
    void processBlock(float* buf, int nframes, bool mute);

    // Called from GUI thread — runs pitch detection, returns detected frequency.
    // Returns 0.0 if not enough data or no pitch detected.
    float detectPitch();

    // Converts frequency to MIDI note number (A4=69=440Hz)
    static int  freqToMidi(float freq);
    // Returns note name string, e.g. "A4", "C#3"
    static std::string midiToName(int midiNote);
    // Returns cents offset from nearest semitone (-50 to +50)
    static float centsOffset(float freq);

private:
    double mSampleRate{44100.0};

    // Ring buffer filled by audio thread, consumed by GUI thread.
    // Protected by mBufMutex; audio uses try_lock (non-blocking).
    std::array<float, kBufSize> mRingBuf{};
    int                          mWritePos{0};
    bool                         mBufferReady{false};
    std::mutex                   mBufMutex;

    // Snapshot used exclusively by detectPitch() (GUI thread only, no mutex needed)
    std::array<float, kBufSize> mSnapBuf{};
};
