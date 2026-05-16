#include "NamProcessor.h"

#include <algorithm>
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
    std::unique_ptr<nam::DSP> newL, newR;
    try {
        newL = nam::get_dsp(std::filesystem::path(path));
        newR = nam::get_dsp(std::filesystem::path(path));
    } catch (const std::exception& e) {
        std::cerr << "[NamProcessor] Failed to load model: " << e.what() << "\n";
        return false;
    }
    if (!newL || !newR) return false;

    newL->Reset(mSampleRate, mMaxBuf); newL->prewarm();
    newR->Reset(mSampleRate, mMaxBuf); newR->prewarm();

    {
        std::lock_guard<std::mutex> lock(mModelMutex);
        int cur = mActiveModel.load();
        if (cur == 0) {
            mModelBL = std::move(newL);
            mModelBR = std::move(newR);
            mActiveModel.store(1, std::memory_order_release);
        } else {
            mModelAL = std::move(newL);
            mModelAR = std::move(newR);
            mActiveModel.store(0, std::memory_order_release);
        }
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
    mMaxBuf = maxBuf;
    mScratchL.resize(maxBuf);
    mScratchR.resize(maxBuf);
}

void NamProcessor::onSampleRateChanged(double /*sr*/)
{
    // mSampleRate is already stored by EffectBase::setSampleRate().
    // The next loadModel() call will Reset() with the new SR.
}

static void runModel(nam::DSP* model, float* buf, int n, std::vector<float>& scratch)
{
    if (static_cast<int>(scratch.size()) < n)
        scratch.resize(n);

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
    processBlockMono(L, n);
    if (inputIsStereo)
        processBlockMonoR(R, n);
    else
        std::copy(L, L + n, R);
    return inputIsStereo;
}
