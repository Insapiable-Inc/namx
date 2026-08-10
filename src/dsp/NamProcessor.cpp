#include "NamProcessor.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>

#include "NAM/get_dsp.h"

NamProcessor::NamProcessor() {}

NamProcessor::~NamProcessor()
{
    // Audio thread must be stopped before this is called.
    std::lock_guard<std::mutex> lock(mModelMutex);
    mModelAL.reset(); mModelAR.reset();
    mModelBL.reset(); mModelBR.reset();
}

bool NamProcessor::loadModel(const std::string& path)
{
    // Load two independent instances from the same file — one per channel.
    // They apply identical weights but maintain separate recurrent states
    // so L and R can diverge (e.g. after a pre-NAM stereo effect).
    //
    // Double-buffer constraint: the audio thread binds the active pair at
    // block start, so at most one load may land per audio block. File I/O +
    // prewarm is orders of magnitude slower than a block, so the UI cannot
    // violate this in practice.
    std::unique_ptr<nam::DSP> newL, newR;
    try {
        newL = nam::get_dsp(std::filesystem::path(path));
        newR = nam::get_dsp(std::filesystem::path(path));
    } catch (const std::exception& e) {
        std::cerr << "[NamProcessor] Failed to load model: " << e.what() << "\n";
        return false;
    }
    if (!newL || !newR) return false;

    int resetBuf = mMaxBuf.load();
    newL->Reset(mSampleRate, resetBuf); newL->prewarm();
    newR->Reset(mSampleRate, resetBuf); newR->prewarm();

    {
        std::lock_guard<std::mutex> lock(mModelMutex);
        // Buffer size may have grown while we were prewarming outside the lock.
        const int cur = mMaxBuf.load();
        if (cur > resetBuf) {
            newL->Reset(mSampleRate, cur); newL->prewarm();
            newR->Reset(mSampleRate, cur); newR->prewarm();
            resetBuf = cur;
        }
        if (mActiveModel.load() == 0) {
            mModelBL = std::move(newL);
            mModelBR = std::move(newR);
            mActiveModel.store(1, std::memory_order_release);
        } else {
            mModelAL = std::move(newL);
            mModelAR = std::move(newR);
            mActiveModel.store(0, std::memory_order_release);
        }
        mModelResetBuf = resetBuf;
        mModelPath = path;
        mModelLoaded.store(true);
    }
    return true;
}

void NamProcessor::unloadModel()
{
    mModelLoaded.store(false);
    mModelPath = "";
}

void NamProcessor::prepareBuffer(int maxBuf)
{
    mMaxBuf.store(maxBuf);
    mScratchL.resize(maxBuf);
    mScratchR.resize(maxBuf);

    // A model Reset() with a smaller max buffer will overrun the NAM core's
    // preallocated storage on a larger block (the core only assert()s, and
    // Release strips asserts) — re-Reset all live instances when we grow.
    // Safe to block here: both backends invoke buffer-size changes on a
    // non-RT thread with no process callback in flight.
    std::lock_guard<std::mutex> lock(mModelMutex);
    if (maxBuf > mModelResetBuf) {
        for (nam::DSP* m : {mModelAL.get(), mModelAR.get(), mModelBL.get(), mModelBR.get()})
            if (m) { m->Reset(mSampleRate, maxBuf); m->prewarm(); }
        mModelResetBuf = maxBuf;
    }
}

void NamProcessor::onSampleRateChanged(double /*sr*/)
{
    // Invalidate the models' Reset() state so the next prepareBuffer() call
    // re-Resets them with the new rate (AudioEngine always calls
    // prepareBuffer right after propagating a sample-rate change).
    std::lock_guard<std::mutex> lock(mModelMutex);
    mModelResetBuf = 0;
}

static void runModel(nam::DSP* model, float* buf, int n, std::vector<float>& scratch)
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

void NamProcessor::processBlockMono(float* buf, int n)
{
    nam::DSP* model = activeModelL();
    if (!mModelLoaded.load() || !model) return;
    runModel(model, buf, n, mScratchL);
}

void NamProcessor::processBlockMonoR(float* buf, int n)
{
    nam::DSP* model = activeModelR();
    if (!mModelLoaded.load() || !model) return;
    runModel(model, buf, n, mScratchR);
}

bool NamProcessor::process(float* L, float* R, int n, bool inputIsStereo)
{
    if (!mEnabled.load()) return inputIsStereo;
    // RT-safety guard: a block larger than what the models were prepared for
    // would overrun the core's buffers — bypass instead (see prepareBuffer).
    if (n > mMaxBuf.load()) {
        if (!inputIsStereo)
            std::copy(L, L + n, R);
        return inputIsStereo;
    }
    processBlockMono(L, n);
    if (inputIsStereo)
        processBlockMonoR(R, n);
    else
        std::copy(L, L + n, R);
    return inputIsStereo;
}
