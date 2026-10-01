#include "IRLoader.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <Eigen/Dense>
#include <wav.h> // AudioDSPTools

#include "Log.h"

static void clearKernelBuffers(std::vector<float> buffers[2], int writePos[2]) {
    for (int ch = 0; ch < 2; ++ch) {
        std::fill(buffers[ch].begin(), buffers[ch].end(), 0.0f);
        writePos[ch] = 0;
    }
}

/// Windowed-sinc resampler. Produces at most maxOut output samples at fsOut
/// from `in` at fsIn, band-limited to the lower of the two Nyquist rates.
static std::vector<float> resampleSinc(const std::vector<float>& in, double fsIn, double fsOut,
                                       size_t maxOut) {
    if (in.empty()) {
        return {};
    }
    const double step = fsIn / fsOut; // input samples per output sample
    const double cutoff = 0.5 * std::min(1.0, 1.0 / step); // cycles per input sample
    const double half = std::ceil(32.0 * std::max(1.0, step)); // kernel half-width, samples
    // Positions are fractional (t), sample indices whole: the casts below convert between them.
    const size_t outLen =
        std::min(maxOut, static_cast<size_t>(std::ceil(static_cast<double>(in.size()) / step)));

    std::vector<float> out(outLen);
    for (size_t i = 0; i < outLen; ++i) {
        const double t = static_cast<double>(i) * step;
        const size_t kMin = static_cast<size_t>(std::max(0.0, std::ceil(t - half)));
        const size_t kMax = std::min(in.size() - 1, static_cast<size_t>(std::floor(t + half)));
        double acc = 0.0;
        for (size_t k = kMin; k <= kMax; ++k) {
            const double u = t - static_cast<double>(k);
            const double x = 2.0 * cutoff * u;
            const double sinc = (std::abs(x) < 1e-9) ? 1.0 : std::sin(M_PI * x) / (M_PI * x);
            // Blackman window over [-half, half]
            const double w =
                0.42 + 0.5 * std::cos(M_PI * u / half) + 0.08 * std::cos(2.0 * M_PI * u / half);
            acc += in[k] * 2.0 * cutoff * sinc * w;
        }
        out[i] = static_cast<float>(acc);
    }
    return out;
}

IRLoader::IRLoader() {
}

IRLoader::~IRLoader() {
    // Audio thread must be stopped before this is called.
    mSlot.publish(nullptr);
    mSlot.reclaim(true);
}

std::unique_ptr<IRLoader::Kernel> IRLoader::buildKernel() const {
    auto k = std::make_unique<Kernel>();

    std::vector<float> samples;
    const bool resample = mTargetRate > 0.0 && mRawRate > 0.0 && mTargetRate != mRawRate;
    if (resample) {
        samples = resampleSinc(mRaw, mRawRate, mTargetRate, kMaxIRLength);
    } else {
        samples = mRaw;
        samples.resize(std::min(samples.size(), kMaxIRLength));
    }

    k->irLen = static_cast<int>(samples.size());
    k->builtRate = resample ? mTargetRate : mRawRate;
    k->ir = std::move(samples);

    // Normalize energy (L2) to 1.0 so the convolution is roughly unity-gain;
    // peak-sample normalization would leave several dB of broadband boost.
    float energy = 0.0f;
    for (float s : k->ir) {
        energy += s * s;
    }
    if (energy > 1e-12f) {
        const float scale = 1.0f / std::sqrt(energy);
        for (float& s : k->ir) {
            s *= scale;
        }
    }

    // Store reversed: the delay line is read oldest→newest, so a reversed IR
    // turns the convolution into a single forward dot product.
    std::reverse(k->ir.begin(), k->ir.end());

    for (int ch = 0; ch < 2; ++ch) {
        k->buffers[ch].assign(2 * kMaxIRLength, 0.0f);
    }
    return k;
}

bool IRLoader::load(const std::string& path) {
    std::vector<float> rawSamples;
    double sampleRate = 0.0;

    auto result = dsp::wav::Load(path.c_str(), rawSamples, sampleRate);
    std::lock_guard<std::mutex> lock(mLoadMutex);
    if (result != dsp::wav::LoadReturnCode::SUCCESS) {
        mLastError = dsp::wav::GetMsgForLoadReturnCode(result);
        if (result == dsp::wav::LoadReturnCode::ERROR_NOT_MONO) {
            mLastError += " Stereo IRs are not supported; convert the file to mono.";
        }
        return false;
    }

    if (rawSamples.empty()) {
        mLastError = "IR file contains no samples.";
        return false;
    }
    mLastError.clear();

    mRaw = std::move(rawSamples);
    mRawRate = sampleRate;
    if (mTargetRate > 0.0 && sampleRate > 0.0 && sampleRate != mTargetRate) {
        qCInfo(lcAudio, "[IRLoader] Resampling IR from %.0f Hz to %.0f Hz", sampleRate,
               mTargetRate);
    }

    mSlot.publish(buildKernel());
    mFilePath = path;
    return true;
}

void IRLoader::unload() {
    std::lock_guard<std::mutex> lock(mLoadMutex);
    mSlot.publish(nullptr);
    mFilePath = "";
    mRaw.clear();
    mRawRate = 0.0;
}

void IRLoader::setTargetSampleRate(double sr) {
    std::lock_guard<std::mutex> lock(mLoadMutex);
    mTargetRate = sr;
    Kernel* k = mSlot.current();
    if (!k || mRaw.empty() || sr <= 0.0 || k->builtRate == sr) {
        return;
    }
    qCInfo(lcAudio, "[IRLoader] Engine rate changed to %.0f Hz — rebuilding IR kernel", sr);
    mSlot.publish(buildKernel());
}

/*static*/ void IRLoader::pushSample(Kernel& k, int ch, float in) {
    float* buf = k.buffers[ch].data();
    const int wp = k.writePos[ch];
    buf[wp] = in;
    buf[wp + k.irLen] = in; // mirror write keeps the window below contiguous
}

/*static*/ float IRLoader::convolveAt(const Kernel& k, int ch) {
    const float* buf = k.buffers[ch].data();
    const int wp = k.writePos[ch];

    // buf[wp+1 .. wp+irLen] is the last irLen samples, oldest→newest;
    // k.ir is reversed, so this dot product is the convolution sum.
    Eigen::Map<const Eigen::VectorXf> ir(k.ir.data(), k.irLen);
    Eigen::Map<const Eigen::VectorXf> sig(buf + wp + 1, k.irLen);
    return ir.dot(sig);
}

/*static*/ void IRLoader::advance(Kernel& k, int ch) {
    if (++k.writePos[ch] >= k.irLen) {
        k.writePos[ch] = 0;
    }
}

void IRLoader::processBlock(float* L, float* R, size_t numFrames, bool inputIsStereo,
                            bool enabled) {
    struct BlockGuard
    {
        RtSlot<Kernel>& s;
        ~BlockGuard() {
            s.endBlock();
        }
    } guard{mSlot};

    // One snapshot per block; the kernel stays alive until endBlock() runs.
    Kernel* kp = mSlot.acquire();
    if (!enabled || !kp || kp->irLen <= 0) {
        return;
    }
    Kernel& k = *kp;

    if (inputIsStereo) {
        for (size_t i = 0; i < numFrames; ++i) {
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
    for (size_t i = 0; i < numFrames; ++i) {
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

void IRLoader::reset() {
    // Audio thread (chain reorder): clear the delay lines of whatever kernel
    // is live. std::fill only — no allocation.
    if (Kernel* k = mSlot.acquire()) {
        clearKernelBuffers(k->buffers, k->writePos);
    }
}
