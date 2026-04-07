#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <mutex>

/// Loads an IR (Impulse Response) WAV file and applies it as a convolution
/// cab simulator using a simple brute-force FIR convolution.
/// The IR is truncated to a reasonable length (e.g. 4096 samples) for performance.
class IRLoader
{
public:
    static constexpr int kMaxIRLength = 4096;

    IRLoader();

    /// Load an IR from a WAV file. Thread-safe: swaps the kernel atomically.
    /// Returns true on success.
    bool load(const std::string& path);

    /// Unload the current IR (bypass mode)
    void unload();

    bool isLoaded() const { return mLoaded.load(std::memory_order_acquire); }
    std::string getFilePath() const { return mFilePath; }

    /// Process a block of samples in-place (mono). Uses channel 0 convolution state.
    void processBlock(float* samples, int numFrames);

    /// Process a stereo block in-place. Uses independent convolution state per channel.
    void processBlockStereo(float* L, float* R, int numFrames);

    /// Reset convolution state
    void reset();

private:
    std::atomic<bool> mLoaded{false};
    std::string       mFilePath;

    // Double-buffered kernels: audio thread reads from active, GUI thread writes inactive
    struct Kernel
    {
        std::vector<float> ir;
        std::vector<float> buffers[2]; // independent delay line per stereo channel
        int                writePos[2] = {0, 0};
        int                irLen       = 0;
    };

    Kernel mKernelA;
    Kernel mKernelB;

    // Which kernel is active in the audio thread (0=A, 1=B)
    std::atomic<int> mActiveIndex{0};
    std::mutex       mSwapMutex;

    Kernel& activeKernel()   { return mActiveIndex.load() == 0 ? mKernelA : mKernelB; }
    Kernel& inactiveKernel() { return mActiveIndex.load() == 0 ? mKernelB : mKernelA; }

    // Inner per-sample convolution for one channel
    static float processChannel(Kernel& k, int ch, float in);
};
