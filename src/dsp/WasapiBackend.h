#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "IAudioBackend.h"

#ifdef _WIN32
struct IMMDeviceEnumerator;
struct IMMDevice;
struct IAudioClient;
struct IAudioCaptureClient;
struct IAudioRenderClient;
using HANDLE_ = void*;
#endif

/// WASAPI implementation of IAudioBackend (Windows-only).
///
/// Skeleton at the moment: compiles, reports "not implemented" on start().
/// Real WASAPI client setup (device enumeration, IAudioClient::Initialize,
/// event-driven render/capture, format negotiation) is filled in later.
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

    // WASAPI has no port graph; defaults from base (supportsPortRouting=false)
    // and empty currentConnections/applyConnections are correct.

private:
#ifdef _WIN32
    void audioThreadMain();

    IMMDeviceEnumerator* mEnumerator    = nullptr;
    IMMDevice*           mCaptureDevice = nullptr;
    IMMDevice*           mRenderDevice  = nullptr;
    IAudioClient*        mCaptureClient = nullptr;
    IAudioClient*        mRenderClient  = nullptr;
    IAudioCaptureClient* mCapture       = nullptr;
    IAudioRenderClient*  mRender        = nullptr;

    HANDLE_ mCaptureEvent = nullptr;
    HANDLE_ mRenderEvent  = nullptr;
    HANDLE_ mStopEvent    = nullptr;

    std::thread mAudioThread;
    std::string mDeviceName;
#endif

    Callback*         mCallback = nullptr;
    std::atomic<bool> mRunning{false};
    double            mSampleRate = 48000.0;
    int               mBufferSize = 0;
};
