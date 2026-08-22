#include "IRLoader.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <Eigen/Dense>

// AudioDSPTools wav loader
#include "wav.h"

static void initKernelBuffers(std::vector<float> buffers[2], int writePos[2])
{
    for (int ch = 0; ch < 2; ++ch)
    {
        buffers[ch].assign(2 * IRLoader::kMaxIRLength, 0.0f);
        writePos[ch] = 0;
    }
}

IRLoader::IRLoader()
{
    initKernelBuffers(mKernelA.buffers, mKernelA.writePos);
    initKernelBuffers(mKernelB.buffers, mKernelB.writePos);
}

bool IRLoader::load(const std::string& path)
{
    // Double-buffer constraint: the audio thread binds the active kernel at
    // block start, so at most one load may land per audio block. WAV I/O is
    // orders of magnitude slower than a block, so the UI cannot violate this.
    std::vector<float> rawSamples;
    double sampleRate = 0.0;

    auto result = dsp::wav::Load(path.c_str(), rawSamples, sampleRate);
    if (result != dsp::wav::LoadReturnCode::SUCCESS)
    {
        mLastError = dsp::wav::GetMsgForLoadReturnCode(result);
        if (result == dsp::wav::LoadReturnCode::ERROR_NOT_MONO)
            mLastError += " Stereo IRs are not supported — convert the file to mono.";
        return false;
    }

    if (rawSamples.empty())
    {
        mLastError = "IR file contains no samples.";
        return false;
    }
    mLastError.clear();

    // Limit to kMaxIRLength samples
    const int irLen = static_cast<int>(std::min<size_t>(rawSamples.size(), kMaxIRLength));

    // Prepare the inactive kernel
    Kernel& k = inactiveKernel();
    k.ir.assign(rawSamples.begin(), rawSamples.begin() + irLen);
    k.irLen = irLen;

    // Normalize energy (L2) to 1.0 so the convolution is roughly unity-gain;
    // peak-sample normalization would leave several dB of broadband boost.
    float energy = 0.0f;
    for (float s : k.ir)
        energy += s * s;
    if (energy > 1e-12f)
    {
        const float scale = 1.0f / std::sqrt(energy);
        for (float& s : k.ir)
            s *= scale;
    }

    // Store reversed: the delay line is read oldest→newest, so a reversed IR
    // turns the convolution into a single forward dot product.
    std::reverse(k.ir.begin(), k.ir.end());

    // Reset both per-channel delay lines in the new kernel
    initKernelBuffers(k.buffers, k.writePos);

    // Atomic swap
    {
        std::lock_guard<std::mutex> lock(mSwapMutex);
        mActiveIndex.store(mActiveIndex.load() == 0 ? 1 : 0);
    }

    mFilePath = path;
    mLoaded.store(true, std::memory_order_release);
    return true;
}

void IRLoader::unload()
{
    mLoaded.store(false, std::memory_order_release);
    mFilePath = "";
    mKernelA.irLen = 0;
    mKernelB.irLen = 0;
}

/*static*/ void IRLoader::pushSample(Kernel& k, int ch, float in)
{
    float*    buf = k.buffers[ch].data();
    const int wp  = k.writePos[ch];
    buf[wp]           = in;
    buf[wp + k.irLen] = in; // mirror write keeps the window below contiguous
}

/*static*/ float IRLoader::convolveAt(const Kernel& k, int ch)
{
    const float* buf = k.buffers[ch].data();
    const int    wp  = k.writePos[ch];

    // buf[wp+1 .. wp+irLen] is the last irLen samples, oldest→newest;
    // k.ir is reversed, so this dot product is the convolution sum.
    Eigen::Map<const Eigen::VectorXf> ir (k.ir.data(),  k.irLen);
    Eigen::Map<const Eigen::VectorXf> sig(buf + wp + 1, k.irLen);
    return ir.dot(sig);
}

/*static*/ void IRLoader::advance(Kernel& k, int ch)
{
    if (++k.writePos[ch] >= k.irLen)
        k.writePos[ch] = 0;
}

void IRLoader::processBlock(float* L, float* R, int numFrames, bool inputIsStereo)
{
    if (!mLoaded.load(std::memory_order_acquire))
        return;

    Kernel& k = activeKernel();
    if (k.irLen <= 0)
        return;

    if (inputIsStereo)
    {
        for (int i = 0; i < numFrames; ++i)
        {
            pushSample(k, 0, L[i]);
            pushSample(k, 1, R[i]);
            L[i] = convolveAt(k, 0);
            R[i] = convolveAt(k, 1);
            advance(k, 0);
            advance(k, 1);
        }
        return;
    }

    // Mono: L == R, so the two delay lines hold identical history. Feed both
    // (a few stores) but run the 4096-tap dot product only once — the R line
    // stays valid for the moment an upstream effect goes stereo.
    for (int i = 0; i < numFrames; ++i)
    {
        const float in = L[i];
        pushSample(k, 0, in);
        pushSample(k, 1, in);
        const float out = convolveAt(k, 0);
        L[i] = out;
        R[i] = out;
        advance(k, 0);
        advance(k, 1);
    }
}

void IRLoader::reset()
{
    initKernelBuffers(mKernelA.buffers, mKernelA.writePos);
    initKernelBuffers(mKernelB.buffers, mKernelB.writePos);
}
