#include "NamProcessor.h"

#include <algorithm>
#include <cassert>
#include "Log.h"
#include <stdexcept>

#include "NAM/get_dsp.h"

NamProcessor::NamProcessor() {}

NamProcessor::~NamProcessor()
{
    // Audio thread must be stopped before this is called.
    std::lock_guard<std::mutex> lock(mModelMutex);
    mSlot.publish(nullptr);
    mSlot.reclaim(true);
}

bool NamProcessor::loadModel(const std::string& path)
{
    // Load two independent instances from the same file — one per channel.
    // They apply identical weights but maintain separate recurrent states
    // so L and R can diverge (e.g. after a pre-NAM stereo effect).
    auto pair = std::make_unique<ModelPair>();
    try {
        pair->L = nam::get_dsp(std::filesystem::path(path));
        pair->R = nam::get_dsp(std::filesystem::path(path));
    } catch (const std::exception& e) {
        mLastError = e.what();
        qCWarning(lcAudio, "[NamProcessor] Failed to load model: %s", e.what());
        return false;
    }
    if (!pair->L || !pair->R) {
        mLastError = "Model loader returned no DSP object.";
        return false;
    }

    // The audio path is strictly mono-in/mono-out per channel instance: the
    // callback hands the core exactly one input and one output pointer. A
    // multichannel model would read/write past those — reject it up front.
    const int inCh  = pair->L->NumInputChannels();
    const int outCh = pair->L->NumOutputChannels();
    if (inCh != 1 || outCh != 1) {
        mLastError = "Model has " + std::to_string(inCh) + " input / "
                   + std::to_string(outCh) + " output channels; only 1-in/1-out models are supported.";
        qCWarning(lcAudio, "[NamProcessor] %s (%s)", mLastError.c_str(), path.c_str());
        return false;
    }
    mLastError.clear();

    // Prewarm outside the lock (slow), then re-check inside it: the buffer
    // size or sample rate may have moved meanwhile.
    int    resetBuf;
    double resetRate;
    {
        std::lock_guard<std::mutex> lock(mModelMutex);
        resetBuf  = mMaxBuf.load();
        resetRate = mModelRate > 0.0 ? mModelRate : mSampleRate;
    }
    pair->L->Reset(resetRate, resetBuf); pair->L->prewarm();
    pair->R->Reset(resetRate, resetBuf); pair->R->prewarm();

    {
        std::lock_guard<std::mutex> lock(mModelMutex);
        const int    curBuf  = mMaxBuf.load();
        const double curRate = mModelRate > 0.0 ? mModelRate : mSampleRate;
        if (curBuf > resetBuf || curRate != resetRate) {
            resetBuf  = std::max(curBuf, resetBuf);
            resetRate = curRate;
            pair->L->Reset(resetRate, resetBuf); pair->L->prewarm();
            pair->R->Reset(resetRate, resetBuf); pair->R->prewarm();
        }
        pair->resetBuf  = resetBuf;
        pair->resetRate = resetRate;
        mSlot.publish(std::move(pair));
        mModelPath = path;
    }
    mFadePending.store(true, std::memory_order_release);
    return true;
}

void NamProcessor::unloadModel()
{
    {
        std::lock_guard<std::mutex> lock(mModelMutex);
        mSlot.publish(nullptr);
        mModelPath = "";
    }
    mFadePending.store(true, std::memory_order_release);
}

void NamProcessor::prepareBuffer(int maxBuf)
{
    // Safe to block and mutate the live pair here: both backends invoke
    // buffer-size changes on a non-RT thread with no process callback in
    // flight, and the scratch buffers are only touched by that callback.
    std::lock_guard<std::mutex> lock(mModelMutex);
    mMaxBuf.store(maxBuf);
    mScratchL.resize(maxBuf);
    mScratchR.resize(maxBuf);

    // A model Reset() with a smaller max buffer will overrun the NAM core's
    // preallocated storage on a larger block (the core only assert()s, and
    // Release strips asserts) — re-Reset the live pair when we grow, or when
    // the sample rate changed since it was prepared.
    ModelPair* p = mSlot.current();
    if (!p) return;
    const double rate = mModelRate > 0.0 ? mModelRate : mSampleRate;
    if (maxBuf > p->resetBuf || rate != p->resetRate) {
        const int buf = std::max(maxBuf, p->resetBuf);
        p->L->Reset(rate, buf); p->L->prewarm();
        p->R->Reset(rate, buf); p->R->prewarm();
        p->resetBuf  = buf;
        p->resetRate = rate;
    }
}

void NamProcessor::onSampleRateChanged(double sr)
{
    // Record the rate; the following prepareBuffer() (AudioEngine always
    // calls it right after propagating a sample-rate change) re-Resets the
    // live pair with it.
    std::lock_guard<std::mutex> lock(mModelMutex);
    mModelRate = sr;
}

/*static*/ void NamProcessor::runModel(nam::DSP* model, float* buf, int n, std::vector<float>& scratch)
{
    // Sized by prepareBuffer(); process() bypasses oversized blocks, so this
    // must never allocate on the audio thread.
    assert(static_cast<int>(scratch.size()) >= n);

    NAM_SAMPLE* inPtr    = buf;
    NAM_SAMPLE* outPtr   = scratch.data();
    NAM_SAMPLE** inPtrs  = &inPtr;
    NAM_SAMPLE** outPtrs = &outPtr;
    model->process(inPtrs, outPtrs, n);
    std::copy(scratch.data(), scratch.data() + n, buf);
}

bool NamProcessor::process(float* L, float* R, int n, bool inputIsStereo)
{
    // Every exit path must count the block so retired pairs get reclaimed.
    struct BlockGuard {
        RtSlot<ModelPair>& s;
        ~BlockGuard() { s.endBlock(); }
    } guard{mSlot};

    if (!mEnabled.load()) return inputIsStereo;

    // One snapshot per block: both channels run the same pair even if a load
    // lands mid-block.
    ModelPair* p = mSlot.acquire();

    // RT-safety guard: a block larger than what the pair was prepared for
    // would overrun the core's buffers — bypass instead (see prepareBuffer).
    const bool canRun = p && n <= p->resetBuf && n <= static_cast<int>(mScratchL.size());
    if (canRun)
    {
        runModel(p->L.get(), L, n, mScratchL);
        if (inputIsStereo)
            runModel(p->R.get(), R, n, mScratchR);
    }
    if (!inputIsStereo)
        std::copy(L, L + n, R);

    // ~15 ms fade-in after a model load/unload masks the discontinuity
    // between the old and new amp (or between amp and dry passthrough).
    if (mFadePending.exchange(false, std::memory_order_acq_rel))
    {
        mFadeTotal     = std::max(1, static_cast<int>(mSampleRate * 0.015));
        mFadeRemaining = mFadeTotal;
    }
    for (int i = 0; i < n && mFadeRemaining > 0; ++i, --mFadeRemaining)
    {
        const float g = 1.0f - static_cast<float>(mFadeRemaining)
                             / static_cast<float>(mFadeTotal);
        L[i] *= g;
        R[i] *= g;
    }

    return inputIsStereo;
}
