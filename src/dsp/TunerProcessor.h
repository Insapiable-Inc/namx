#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "EffectBase.h"

/// Chromatic tuner using NSDF (YIN-style) pitch detection.
/// The audio thread fills a ring buffer; an internal worker thread runs the
/// detection (~1M MACs — far too heavy for the GUI thread); the GUI polls
/// lastPitch(). When enabled and mute is on, zeros the audio. When disabled,
/// passes through unchanged.
class TunerProcessor : public EffectBase
{
public:
    static constexpr int kBufSize = 4096;

    TunerProcessor();
    ~TunerProcessor() override;

    void reset() override;

    void setMute(bool m) {
        mMute.store(m);
    }
    bool mute() const {
        return mMute.load();
    }

    /// Latest pitch computed by the worker thread, in Hz (0.0 = no pitch).
    float lastPitch() const {
        return mLastPitch.load();
    }

    /// `refHz` = the A4 reference (concert pitch 440; 420–460 for bands
    /// and records tuned off it).
    static int freqToMidi(float freq, float refHz = 440.0f);
    static std::string midiToName(int midiNote);
    static float centsOffset(float freq, float refHz = 440.0f);

protected:
    void onSampleRateChanged(double sr) override {
        mFs.store(static_cast<float>(sr));
    }

    // Accumulates samples into ring buffer; applies mute if set.
    void processBlockMono(float* buf, int nframes) override;

    // Stereo input (tuner placed after a stereo effect): detector reads the
    // L/R average, mute silences both channels.
    void processBlockDual(float* L, float* R, int nframes) override;

private:
    // Worker loop: computes pitch ~10x/s while the tuner is enabled.
    void workerMain();
    float detectPitch();

    std::atomic<bool> mMute{false};

    // Ring buffer filled by audio thread, consumed by the worker thread.
    // Protected by mBufMutex; audio uses try_lock (non-blocking).
    std::array<float, kBufSize> mRingBuf{};
    int mWritePos{0};
    bool mBufferReady{false};
    std::mutex mBufMutex;

    // Snapshot and NSDF scratch used exclusively by detectPitch() (worker thread only)
    std::array<float, kBufSize> mSnapBuf{};
    std::vector<float> mNsdf;

    std::thread mWorker;
    std::mutex mWorkMutex;
    std::condition_variable mWorkCv;
    bool mStop{false}; // guarded by mWorkMutex
    std::atomic<float> mLastPitch{0.0f};
    std::atomic<float> mFs{44100.0f}; // worker-safe copy of the sample rate
};
