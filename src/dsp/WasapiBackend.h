#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "IAudioBackend.h"

#ifdef _WIN32
// Forward-declare the WASAPI COM interfaces so the header is self-contained
// (no <windows.h> pollution for files that include AudioEngine.h transitively).
struct IMMDeviceEnumerator;
struct IMMDevice;
struct IAudioClient;
struct IAudioCaptureClient;
struct IAudioRenderClient;
#endif

/// WASAPI implementation of IAudioBackend (Windows-only).
///
/// Shared-mode, event-driven. Captures from the default communications/console
/// input device and renders to the default render device. Internal sample
/// format is float32 (the WASAPI mix format on modern Windows is almost always
/// already 32-bit float, so no resampling is needed in the common path).
///
/// Threading: an internal `mAudioThread` runs the capture+render loop. The
/// thread blocks on the capture-ready event, pulls a block, hands it to the
/// AudioEngine callback, then writes the result to the render endpoint.
class WasapiBackend : public IAudioBackend
{
public:
    WasapiBackend();
    ~WasapiBackend() override;

    bool start(const std::string& clientName, Callback* cb) override;
    void stop() override;

    bool   isRunning()  const override { return mRunning.load(); }
    double sampleRate() const override { return mSampleRate; }
    int    bufferSize() const override { return mBufferSize; }
    std::string statusString() const override;

    // WASAPI has no port graph — defaults from base (supportsPortRouting=false)
    // and empty currentConnections/applyConnections apply.

private:
#ifdef _WIN32
    void audioThreadMain();
    bool initCapture();
    bool initRender();
    void teardown();

    IMMDeviceEnumerator* mEnumerator     = nullptr;
    IMMDevice*           mCaptureDevice  = nullptr;
    IMMDevice*           mRenderDevice   = nullptr;
    IAudioClient*        mCaptureClient  = nullptr;
    IAudioClient*        mRenderClient   = nullptr;
    IAudioCaptureClient* mCapture        = nullptr;
    IAudioRenderClient*  mRender         = nullptr;

    // HANDLEs from <synchapi.h>; void* keeps the header windows.h-free.
    void* mCaptureEvent = nullptr;
    void* mRenderEvent  = nullptr;
    void* mStopEvent    = nullptr;

    std::thread mAudioThread;
    std::string mDeviceName;
    bool        mComInitialized = false;
#endif

    Callback*         mCallback = nullptr;
    std::atomic<bool> mRunning{false};
    double            mSampleRate = 48000.0;
    int               mBufferSize = 0;
    int               mCaptureChannels = 0;
    int               mRenderChannels  = 0;
};
