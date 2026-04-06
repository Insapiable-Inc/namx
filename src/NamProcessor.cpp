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
    mModelA.reset();
    mModelB.reset();
}

bool NamProcessor::loadModel(const std::string& path)
{
    std::unique_ptr<nam::DSP> newModel;
    try {
        newModel = nam::get_dsp(std::filesystem::path(path));
    } catch (const std::exception& e) {
        std::cerr << "[NamProcessor] Failed to load model: " << e.what() << "\n";
        return false;
    }
    if (!newModel) return false;

    newModel->Reset(mSampleRate, mMaxBuf);
    newModel->prewarm();

    {
        std::lock_guard<std::mutex> lock(mModelMutex);
        int cur = mActiveModel.load();
        if (cur == 0) {
            mModelB = std::move(newModel);
            mActiveModel.store(1, std::memory_order_release);
        } else {
            mModelA = std::move(newModel);
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
    mScratch.resize(maxBuf);
}

void NamProcessor::onSampleRateChanged(double /*sr*/)
{
    // mSampleRate is already stored by EffectBase::setSampleRate().
    // The next loadModel() call will Reset() with the new SR.
}

void NamProcessor::processBlockMono(float* buf, int n)
{
    nam::DSP* model = activeModel();
    if (!mModelLoaded.load() || !model) return;

    if (static_cast<int>(mScratch.size()) < n)
        mScratch.resize(n);

    NAM_SAMPLE* inPtr  = buf;
    NAM_SAMPLE* outPtr = mScratch.data();
    NAM_SAMPLE** inPtrs  = &inPtr;
    NAM_SAMPLE** outPtrs = &outPtr;
    model->process(inPtrs, outPtrs, n);
    std::copy(mScratch.data(), mScratch.data() + n, buf);
}
