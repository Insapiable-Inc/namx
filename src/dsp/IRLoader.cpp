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
    std::vector<float> rawSamples;
    double sampleRate = 0.0;

    auto result = dsp::wav::Load(path.c_str(), rawSamples, sampleRate);
    if (result != dsp::wav::LoadReturnCode::SUCCESS)
        return false;

    if (rawSamples.empty())
        return false;

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

/*static*/ float IRLoader::processChannel(Kernel& k, int ch, float in)
{
    float*    buf = k.buffers[ch].data();
    const int wp  = k.writePos[ch];

    buf[wp]           = in;
    buf[wp + k.irLen] = in; // mirror write keeps the window below contiguous

    // buf[wp+1 .. wp+irLen] is the last irLen samples, oldest→newest;
    // k.ir is reversed, so this dot product is the convolution sum.
    Eigen::Map<const Eigen::VectorXf> ir (k.ir.data(),  k.irLen);
    Eigen::Map<const Eigen::VectorXf> sig(buf + wp + 1, k.irLen);
    const float out = ir.dot(sig);

    if (++k.writePos[ch] >= k.irLen)
        k.writePos[ch] = 0;

    return out;
}

void IRLoader::processBlockStereo(float* L, float* R, int numFrames)
{
    if (!mLoaded.load(std::memory_order_acquire))
        return;

    Kernel& k = activeKernel();
    if (k.irLen <= 0)
        return;

    for (int i = 0; i < numFrames; ++i)
    {
        L[i] = processChannel(k, 0, L[i]);
        R[i] = processChannel(k, 1, R[i]);
    }
}

void IRLoader::reset()
{
    initKernelBuffers(mKernelA.buffers, mKernelA.writePos);
    initKernelBuffers(mKernelB.buffers, mKernelB.writePos);
}
