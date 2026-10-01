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
    static constexpr size_t K_BUF_SIZE = 4096;

    TunerProcessor();
    ~TunerProcessor() override;

    void reset() override;

    void set_mute(bool m) {
        m_mute.store(m);
    }
    bool mute() const {
        return m_mute.load();
    }

    /// Latest pitch computed by the worker thread, in Hz (0.0 = no pitch).
    float last_pitch() const {
        return m_last_pitch.load();
    }

    /// `refHz` = the A4 reference (concert pitch 440; 420–460 for bands
    /// and records tuned off it).
    static int freq_to_midi(float freq, float ref_hz = 440.0f);
    static std::string midi_to_name(int midi_note);
    static float cents_offset(float freq, float ref_hz = 440.0f);

protected:
    void on_sample_rate_changed(double sr) override {
        m_fs.store(static_cast<float>(sr));
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

    std::atomic<bool> m_mute{false};

    // Ring buffer filled by audio thread, consumed by the worker thread.
    // Protected by mBufMutex; audio uses try_lock (non-blocking).
    std::array<float, K_BUF_SIZE> m_ring_buf{};
    size_t m_write_pos{0};
    bool m_buffer_ready{false};
    std::mutex m_buf_mutex;

    // Snapshot and NSDF scratch used exclusively by detectPitch() (worker thread only)
    std::array<float, K_BUF_SIZE> m_snap_buf{};
    std::vector<float> m_nsdf;

    std::thread m_worker;
    std::mutex m_work_mutex;
    std::condition_variable m_work_cv;
    bool m_stop{false}; // guarded by mWorkMutex
    std::atomic<float> m_last_pitch{0.0f};
    std::atomic<float> m_fs{44100.0f}; // worker-safe copy of the sample rate
};
