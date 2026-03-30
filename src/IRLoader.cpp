#include "IRLoader.h"

#include <algorithm>
#include <cstring>

// AudioDSPTools wav loader
#include "wav.h"

IRLoader::IRLoader()
{
    mKernelA.buffer.resize(kMaxIRLength, 0.0f);
    mKernelB.buffer.resize(kMaxIRLength, 0.0f);
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

    // Take only the first channel if stereo (interleaved), and limit length
    const int irLen = static_cast<int>(std::min<size_t>(rawSamples.size(), kMaxIRLength));

    // Prepare the inactive kernel
    Kernel& k = inactiveKernel();
    k.ir.assign(rawSamples.begin(), rawSamples.begin() + irLen);
    k.irLen   = irLen;
    k.writePos = 0;
    k.buffer.assign(kMaxIRLength, 0.0f);

    // Normalize peak to 1.0
    float peak = 0.0f;
    for (float s : k.ir)
        peak = std::max(peak, std::abs(s));
    if (peak > 1e-6f)
        for (float& s : k.ir)
            s /= peak;

    // Atomic swap
    {
        std::lock_guard<std::mutex> lock(mSwapMutex);
        mActiveIndex.store(mActiveIndex.load() == 0 ? 1 : 0);
    }

    mFilePath = path;
    mLoaded   = true;
    return true;
}

void IRLoader::unload()
{
    mLoaded   = false;
    mFilePath = "";
    mKernelA.irLen = 0;
    mKernelB.irLen = 0;
}

float IRLoader::processSample(float in)
{
    if (!mLoaded)
        return in;

    Kernel& k = activeKernel();
    if (k.irLen <= 0)
        return in;

    // Write input into circular buffer
    k.buffer[k.writePos] = in;

    // Convolve
    float out = 0.0f;
    int   pos = k.writePos;
    for (int i = 0; i < k.irLen; ++i)
    {
        out += k.ir[i] * k.buffer[pos];
        if (--pos < 0)
            pos = k.irLen - 1;
    }

    if (++k.writePos >= k.irLen)
        k.writePos = 0;

    return out;
}

void IRLoader::processBlock(float* samples, int numFrames)
{
    if (!mLoaded)
        return;

    for (int i = 0; i < numFrames; ++i)
        samples[i] = processSample(samples[i]);
}

void IRLoader::reset()
{
    mKernelA.buffer.assign(kMaxIRLength, 0.0f);
    mKernelA.writePos = 0;
    mKernelB.buffer.assign(kMaxIRLength, 0.0f);
    mKernelB.writePos = 0;
}
