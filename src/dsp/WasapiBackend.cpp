#include "WasapiBackend.h"

#include <iostream>
#include <sstream>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <mmdeviceapi.h>
#  include <audioclient.h>
#endif

std::unique_ptr<IAudioBackend> IAudioBackend::create()
{
    return std::make_unique<WasapiBackend>();
}

WasapiBackend::WasapiBackend() = default;

WasapiBackend::~WasapiBackend()
{
    stop();
}

bool WasapiBackend::start(const std::string& /*clientName*/, Callback* cb)
{
    mCallback = cb;
#ifdef _WIN32
    // TODO: full WASAPI client setup.
    //   1. CoInitializeEx for this thread.
    //   2. Create IMMDeviceEnumerator.
    //   3. Default capture + render endpoints.
    //   4. IAudioClient::GetMixFormat + Initialize (shared mode, event-driven).
    //   5. IAudioCaptureClient + IAudioRenderClient handles.
    //   6. Spawn audio thread, signal mCallback->onBufferSizeChanged once.
    //   7. Audio thread loop: WaitForMultipleObjects on capture+render+stop
    //      events, read capture buffer, call mCallback->onAudio, write render
    //      buffer.
    std::cerr << "[WasapiBackend] start() not yet implemented\n";
    return false;
#else
    (void)cb;
    return false;
#endif
}

void WasapiBackend::stop()
{
    if (!mRunning.load()) return;
    mRunning.store(false);
#ifdef _WIN32
    // TODO: signal mStopEvent, join audio thread, release COM objects in
    // reverse acquisition order, CoUninitialize.
#endif
    mCallback = nullptr;
}

std::string WasapiBackend::statusString() const
{
#ifdef _WIN32
    if (!mRunning.load())
        return "WASAPI  ●  not started";
    std::ostringstream o;
    o << "WASAPI  ●  " << mDeviceName << "  ●  " << static_cast<int>(mSampleRate) << " Hz";
    return o.str();
#else
    return "WASAPI  ●  not supported on this platform";
#endif
}

#ifdef _WIN32
void WasapiBackend::audioThreadMain()
{
    // TODO: implement real-time render/capture loop using event-driven WASAPI.
}
#endif
