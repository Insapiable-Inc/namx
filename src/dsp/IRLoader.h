#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "RtSlot.h"

/// Loads an IR (Impulse Response) WAV file and applies it as a convolution
/// cab simulator using a simple brute-force FIR convolution.
/// The IR is resampled to the engine rate, then truncated to kMaxIRLength.
class IRLoader
{
public:
    static constexpr size_t K_MAX_IR_LENGTH = 4096;

    IRLoader();
    ~IRLoader();

    /// Load an IR from a WAV file. Never blocks the audio thread: the new
    /// kernel is published through an RtSlot and the old one is destroyed
    /// only once the audio thread has left it. Returns true on success.
    bool load(const std::string& path);

    /// Unload the current IR (bypass mode)
    void unload();

    bool is_loaded() const {
        return m_slot.current() != nullptr;
    }
    std::string get_file_path() const {
        return m_file_path;
    }

    /// Human-readable reason the last load() returned false (GUI thread only).
    std::string get_last_error() const {
        return m_last_error;
    }

    /// Engine sample rate. If an IR is loaded whose kernel was built for a
    /// different rate, it is rebuilt (resampled) and republished. Called
    /// with no audio callback in flight (backend buffer-size hook).
    void set_target_sample_rate(double sr);

    /// Free retired kernels; `audioStopped` frees unconditionally.
    void reclaim_retired(bool audio_stopped) {
        m_slot.reclaim(audio_stopped);
    }

    /// Process a block in-place. inputIsStereo=false means L and R hold the
    /// same signal: the convolution then runs once (L) and is copied to R,
    /// while both per-channel delay lines are still fed so the R state is
    /// valid the moment a stereo block arrives. inputIsStereo=true convolves
    /// each channel with its own state. `enabled=false` only counts the
    /// block (needed so retired kernels can be reclaimed) and leaves the
    /// audio untouched. Audio thread only.
    void process_block(float* l, float* r, size_t num_frames, bool input_is_stereo, bool enabled);

    /// Reset convolution state of the active kernel. Audio thread only.
    void reset();

private:
    struct Kernel
    {
        std::vector<float> ir; // stored REVERSED so the convolution is a forward dot product
        std::vector<float> buffers[2]; // per-channel delay line, 2*kMaxIRLength: each sample is
                                       // written twice (writePos and writePos+irLen) so the last
                                       // irLen samples are always contiguous — no wrap in the loop
        int write_pos[2] = {0, 0};
        int ir_len = 0;
        double built_rate = 0.0; // engine rate this kernel was resampled to
    };

    RtSlot<Kernel> m_slot;
    std::mutex m_load_mutex; // load/unload (GUI) vs. setTargetSampleRate (backend thread)

    // Owner-side state (guarded by mLoadMutex)
    std::string m_file_path;
    std::string m_last_error;
    std::vector<float> m_raw; // native samples of the loaded file
    double m_raw_rate{0.0}; // their sample rate
    double m_target_rate{0.0};

    std::unique_ptr<Kernel> build_kernel() const;

    // Per-sample steps for one channel: push the input into the delay line,
    // dot it with the (reversed) IR, advance the write position.
    static void push_sample(Kernel& k, int ch, float in);
    static float convolve_at(const Kernel& k, int ch);
    static void advance(Kernel& k, int ch);
};
