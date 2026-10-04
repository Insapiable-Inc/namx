#pragma once

#include <atomic>
#include <complex>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <signalsmith-linear/fft.h>

#include "Common.h"
#include "RtSlot.h"

namespace namx
{

// Loads an IR (Impulse Response) WAV file and applies it as a convolution cab simulator.
// The IR is resampled to the engine rate and capped at MAX_IR_SECONDS (a longer one is
// faded out over its last FADE_MS instead of cut).
//
// Convolution with no added latency (Gardner-style split): the first HEAD taps run
// directly, sample by sample; the rest runs as uniformly partitioned FFT convolution
// (overlap-save, partitions of HEAD samples, a frequency-domain delay line). The tail
// starts HEAD samples in, so each block's FFT work produces output for the *next* block
// — on time. The FFT runs once per HEAD samples, whatever the host block size.
class IRLoader
{
public:
    static constexpr size_t HEAD = 64; // direct taps = partition size
    static constexpr double MAX_IR_SECONDS = 1.0;
    static constexpr double FADE_MS = 10.0;

    IRLoader();
    ~IRLoader();

    // Load an IR from a WAV file. Never blocks the audio thread: the new
    // kernel is published through an RtSlot and the old one is destroyed
    // only once the audio thread has left it. Returns true on success.
    bool load(const string& path);

    // Unload the current IR (bypass mode)
    void unload();

    bool is_loaded() const {
        return slot.current() != nullptr;
    }
    string get_file_path() const {
        return file_path;
    }

    // Human-readable reason the last load() returned false (GUI thread only).
    string get_last_error() const {
        return last_error;
    }

    // Engine sample rate. If an IR is loaded whose kernel was built for a
    // different rate, it is rebuilt (resampled) and republished. Called
    // with no audio callback in flight (backend buffer-size hook).
    void set_target_sample_rate(double sr);

    // Free retired kernels; `audioStopped` frees unconditionally. Under the load
    // mutex: set_target_sample_rate publishes from the backend's notification thread
    // while the GUI reclaims (audit 2026-10-04, Fable item 06).
    void reclaim_retired(bool audio_stopped) {
        std::lock_guard<std::mutex> lock(load_mutex);
        slot.reclaim(audio_stopped);
    }

    // Process a block in-place. inputIsStereo=false means L and R hold the
    // same signal: the convolution then runs once (L) and is copied to R,
    // while both per-channel delay lines are still fed so the R state is
    // valid the moment a stereo block arrives. inputIsStereo=true convolves
    // each channel with its own state. `enabled=false` only counts the
    // block (needed so retired kernels can be reclaimed) and leaves the
    // audio untouched. Audio thread only.
    void process_block(float* l, float* r, size_t num_frames, bool input_is_stereo, bool enabled);

    // Reset convolution state of the active kernel. Audio thread only.
    void reset();

private:
    using Complex = std::complex<float>;

    // One channel's convolution state.
    struct Channel
    {
        // Head: delay line of 2 × HEAD, each sample written twice (pos and pos + HEAD) so
        // the last HEAD samples are always contiguous.
        vector<float> head_line;
        size_t head_pos = 0;
        vector<float> frame; // 2 × HEAD: previous block, then the block being filled
        vector<Complex> fdl; // partitions × HEAD spectra of past frames (a ring)
        size_t fdl_newest = 0;
        vector<float> tail_out; // HEAD: the tail's output for the block being filled
        size_t pos = 0; // samples into the current block
    };

    struct Kernel
    {
        vector<float> head_ir; // first HEAD taps, REVERSED (a forward dot product)
        vector<Complex> tail_spectra; // partitions × HEAD: the rest of the IR, per partition
        size_t partitions = 0;
        signalsmith::linear::RealFFT<float> fft{2 * HEAD};
        vector<Complex> spectrum; // HEAD: scratch for one block's computation
        vector<float> time; // 2 × HEAD: scratch
        Channel channels[2];
        bool right_in_sync = false; // false while mono blocks run the left channel only
        double built_rate = 0.0; // engine rate this kernel was resampled to
    };

    RtSlot<Kernel> slot;
    std::mutex load_mutex; // load/unload (GUI) vs. setTargetSampleRate (backend thread)

    // Owner-side state (guarded by load_mutex)
    string file_path;
    string last_error;
    vector<float> raw; // native samples of the loaded file
    double raw_rate{0.0}; // their sample rate
    size_t raw_full{0}; // the file's length in samples (raw keeps only what the kernel uses)
    double target_rate{0.0};

    unique_ptr<Kernel> build_kernel() const;

    static float process_sample(Kernel& k, Channel& c, float in);
    static void compute_tail(Kernel& k, Channel& c);
    static void clear(Channel& c);
};

} // namespace namx
