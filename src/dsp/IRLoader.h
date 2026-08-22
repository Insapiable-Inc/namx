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

    /// Human-readable reason the last load() returned false (GUI thread only).
    std::string lastError() const { return mLastError; }

    /// Process a block in-place. inputIsStereo=false means L and R hold the
    /// same signal: the convolution then runs once (L) and is copied to R,
    /// while both per-channel delay lines are still fed so the R state is
    /// valid the moment a stereo block arrives. inputIsStereo=true convolves
    /// each channel with its own state.
    void processBlock(float* L, float* R, int numFrames, bool inputIsStereo);

    /// Reset convolution state
    void reset();

private:
    std::atomic<bool> mLoaded{false};
    std::string       mFilePath;
    std::string       mLastError;

    // Double-buffered kernels: audio thread reads from active, GUI thread writes inactive
    struct Kernel
    {
        std::vector<float> ir;         // stored REVERSED so the convolution is a forward dot product
        std::vector<float> buffers[2]; // per-channel delay line, 2*kMaxIRLength: each sample is
                                       // written twice (writePos and writePos+irLen) so the last
                                       // irLen samples are always contiguous — no wrap in the loop
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

    // Per-sample steps for one channel: push the input into the delay line,
    // dot it with the (reversed) IR, advance the write position.
    static void  pushSample(Kernel& k, int ch, float in);
    static float convolveAt(const Kernel& k, int ch);
    static void  advance(Kernel& k, int ch);
};
