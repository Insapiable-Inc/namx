#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "Common.h"
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

namespace npl
{

/// WASAPI implementation of IAudioBackend (Windows-only).
///
/// Shared-mode, event-driven. Captures from a user-selectable or default
/// input device and renders to a user-selectable or default output device.
/// Internal sample format is float32 (the WASAPI mix format on modern
/// Windows is almost always 32-bit float, so no resampling is needed in
/// the common path).
///
/// Threading: an internal `audio_thread` runs the capture+render loop. The
/// thread blocks on the capture-ready event, pulls a block, hands it to the
/// AudioEngine callback, then writes the result to the render endpoint.
class WasapiBackend : public IAudioBackend
{
public:
    WasapiBackend();
    ~WasapiBackend() override;

    bool start(const string& client_name, Callback* cb) override;
    void stop() override;

    bool is_running() const override {
        return running.load();
    }
    double get_sample_rate() const override {
        return sample_rate;
    }
    int get_buffer_size() const override {
        return buffer_size;
    }
    string status_string() const override;

    vector<DeviceInfo> enumerate_input_devices() const override;
    vector<DeviceInfo> enumerate_output_devices() const override;
    bool start_with_devices(const string& client_name, Callback* cb, const string& input_device_id,
                            const string& output_device_id) override;

    // Capture packets flagged DATA_DISCONTINUITY (the engine dropped input).
    int xrun_count() const override {
        return xruns.load(std::memory_order_relaxed);
    }
    void reset_xruns() override {
        xruns.store(0, std::memory_order_relaxed);
    }

    // WASAPI has no port graph — defaults from base (supports_port_routing=false)
    // and empty current_connections/apply_connections apply.

private:
#ifdef _WIN32
    void audio_thread_main();
    bool init_capture();
    bool init_render();
    void teardown();

    IMMDevice* open_device_by_id(const string& deviceId) const;

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* capture_device = nullptr;
    IMMDevice* render_device = nullptr;
    IAudioClient* capture_client = nullptr;
    IAudioClient* render_client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    IAudioRenderClient* render = nullptr;

    // HANDLEs from <synchapi.h>; void* keeps the header windows.h-free.
    void* capture_event = nullptr;
    void* render_event = nullptr;
    void* stop_event = nullptr;

    std::thread audio_thread;
    string device_name;
    bool com_initialized = false;
#endif

    Callback* callback = nullptr;
    atomic<bool> running{false};
    atomic<int> xruns{0};
    double sample_rate = 48000.0;
    int buffer_size = 0;
    int capture_channels = 0;
    int render_channels = 0;
};

} // namespace npl
