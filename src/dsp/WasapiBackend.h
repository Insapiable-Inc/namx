#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <vector>

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
/// Shared-mode, event-driven. Captures from a user-selectable or default
/// input device and renders to a user-selectable or default output device.
/// Internal sample format is float32 (the WASAPI mix format on modern
/// Windows is almost always 32-bit float, so no resampling is needed in
/// the common path).
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

    bool isRunning() const override {
        return mRunning.load();
    }
    double sampleRate() const override {
        return mSampleRate;
    }
    int bufferSize() const override {
        return mBufferSize;
    }
    std::string statusString() const override;

    std::vector<DeviceInfo> enumerateInputDevices() const override;
    std::vector<DeviceInfo> enumerateOutputDevices() const override;
    bool startWithDevices(const std::string& clientName, Callback* cb,
                          const std::string& inputDeviceId,
                          const std::string& outputDeviceId) override;

    // Capture packets flagged DATA_DISCONTINUITY (the engine dropped input).
    int xrunCount() const override {
        return mXruns.load(std::memory_order_relaxed);
    }
    void resetXruns() override {
        mXruns.store(0, std::memory_order_relaxed);
    }

    // WASAPI has no port graph — defaults from base (supportsPortRouting=false)
    // and empty currentConnections/applyConnections apply.

private:
#ifdef _WIN32
    void audioThreadMain();
    bool initCapture();
    bool initRender();
    void teardown();

    IMMDevice* openDeviceById(const std::string& deviceId) const;

    IMMDeviceEnumerator* mEnumerator = nullptr;
    IMMDevice* mCaptureDevice = nullptr;
    IMMDevice* mRenderDevice = nullptr;
    IAudioClient* mCaptureClient = nullptr;
    IAudioClient* mRenderClient = nullptr;
    IAudioCaptureClient* mCapture = nullptr;
    IAudioRenderClient* mRender = nullptr;

    // HANDLEs from <synchapi.h>; void* keeps the header windows.h-free.
    void* mCaptureEvent = nullptr;
    void* mRenderEvent = nullptr;
    void* mStopEvent = nullptr;

    std::thread mAudioThread;
    std::string mDeviceName;
    bool mComInitialized = false;
#endif

    Callback* mCallback = nullptr;
    std::atomic<bool> mRunning{false};
    std::atomic<int> mXruns{0};
    double mSampleRate = 48000.0;
    int mBufferSize = 0;
    int mCaptureChannels = 0;
    int mRenderChannels = 0;
};
