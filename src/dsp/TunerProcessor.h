#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Common.h"
#include "EffectBase.h"

namespace namx
{

// Chromatic tuner using NSDF (YIN-style) pitch detection.
// The audio thread fills a ring buffer; an internal worker thread runs the
// detection (~1M MACs — far too heavy for the GUI thread); the GUI polls
// lastPitch(). When enabled and mute is on, zeros the audio. When disabled,
// passes through unchanged.
class TunerProcessor : public EffectBase
{
public:
    static constexpr size_t BUF_SIZE = 4096;

    TunerProcessor();
    ~TunerProcessor() override;

    void reset() override;

    void set_mute(bool m) {
        mute.store(m);
    }
    bool get_mute() const {
        return mute.load();
    }

    // Latest pitch computed by the worker thread, in Hz (0.0 = no pitch).
    float get_last_pitch() const {
        return last_pitch.load();
    }

    // `refHz` = the A4 reference (concert pitch 440; 420–460 for bands
    // and records tuned off it).
    static int freq_to_midi(float freq, float ref_hz = 440.0f);
    static string midi_to_name(int midi_note);
    static float cents_offset(float freq, float ref_hz = 440.0f);

protected:
    void on_sample_rate_changed(double sr) override {
        fs.store(static_cast<float>(sr));
    }

    // Accumulates samples into ring buffer; applies mute if set.
    void process_block_mono(float* buf, size_t nframes) override;

    // Stereo input (tuner placed after a stereo effect): detector reads the
    // L/R average, mute silences both channels.
    void process_block_dual(float* l, float* r, size_t nframes) override;

private:
    // Worker loop: computes pitch ~10x/s while the tuner is enabled.
    void worker_main();
    float detect_pitch();

    atomic<bool> mute{true};

    // Ring buffer filled by audio thread, consumed by the worker thread.
    // Protected by buf_mutex; audio uses try_lock (non-blocking).
    array<float, BUF_SIZE> ring_buf{};
    size_t write_pos{0};
    bool buffer_ready{false};
    std::mutex buf_mutex;

    // Snapshot and NSDF scratch used exclusively by detectPitch() (worker thread only)
    array<float, BUF_SIZE> snap_buf{};
    vector<float> nsdf;

    std::thread worker;
    std::mutex work_mutex;
    std::condition_variable work_cv;
    bool stop{false}; // guarded by work_mutex
    atomic<float> last_pitch{0.0f};
    atomic<float> fs{44100.0f}; // worker-safe copy of the sample rate
};

} // namespace namx
