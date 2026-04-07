#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <string>

#include "EffectBase.h"

/// Chromatic tuner using NSDF (YIN-style) pitch detection.
/// Buffers input samples (audio thread) and computes pitch (GUI thread).
/// When enabled and mute is on, zeros the audio. When disabled, passes through unchanged.
class TunerProcessor : public EffectBase
{
public:
    static constexpr int kBufSize = 4096;

    TunerProcessor();

    void reset() override;

    void setMute(bool m) { mMute.store(m); }
    bool mute()    const { return mMute.load(); }

    // Called from GUI thread — runs pitch detection, returns detected frequency.
    // Returns 0.0 if not enough data or no pitch detected.
    float detectPitch();

    static int         freqToMidi(float freq);
    static std::string midiToName(int midiNote);
    static float       centsOffset(float freq);

protected:
    void onSampleRateChanged(double /*sr*/) override {}

    // Accumulates samples into ring buffer; applies mute if set.
    void processBlockMono(float* buf, int nframes) override;

private:
    std::atomic<bool> mMute{false};

    // Ring buffer filled by audio thread, consumed by GUI thread.
    // Protected by mBufMutex; audio uses try_lock (non-blocking).
    std::array<float, kBufSize> mRingBuf{};
    int                          mWritePos{0};
    bool                         mBufferReady{false};
    std::mutex                   mBufMutex;

    // Snapshot used exclusively by detectPitch() (GUI thread only)
    std::array<float, kBufSize> mSnapBuf{};
};
