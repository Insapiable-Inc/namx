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

namespace namx
{

// WASAPI implementation of IAudioBackend (Windows-only).
//
// Event-driven. Captures from a user-selectable or default input device and
// renders to a user-selectable or default output device.
//
// Shared mode (the default): Windows mixes, the stream uses the device's mix
// format (float32 on modern Windows) and the system's ~10 ms period.
// Exclusive mode (opt-in, set_exclusive_mode): the app owns both devices, picks
// a format they take natively (float32, int32, 24-in-32, packed int24 or int16,
// converted here) at the render device's rate, and the devices' minimum period —
// the lowest latency WASAPI offers. If either device refuses, the stream falls
// back to shared mode (exclusive_active() then reports false).
//
// Threading: an internal `audio_thread` runs the capture+render loop. The
// thread blocks on the capture-ready event, pulls a block, hands it to the
// AudioEngine callback, then writes the result to the render endpoint.
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
        return buffer_size.load(std::memory_order_relaxed);
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

    bool supports_exclusive_mode() const override {
        return true;
    }
    void set_exclusive_mode(bool on) override {
        exclusive_wanted = on;
    }
    bool exclusive_active() const override {
        return exclusive;
    }
    // Exclusive mode: capture + render buffers (an estimate; -1 in shared mode).
    int io_latency_frames() const override;

    // Sample layouts a stream can use (exclusive mode converts; shared is FLOAT32).
    enum class SampleType
    {
        FLOAT32,
        INT32, // also 24-bit samples left-justified in 32-bit containers
        INT24, // packed, 3 bytes
        INT16
    };

private:
#ifdef _WIN32
    void audio_thread_main();
    bool init_capture();
    bool init_render();
    bool init_exclusive();
    void release_streams(); // the clients and services, keeping the devices
    void teardown();

    IMMDevice* open_device_by_id(const string& device_id) const;

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
    atomic<int> buffer_size{0}; // init paths and the audio thread write, the GUI reads
    int capture_channels = 0;
    int render_channels = 0;
    SampleType capture_type = SampleType::FLOAT32;
    SampleType render_type = SampleType::FLOAT32;
    int render_buffer_frames = 0; // exclusive: frames written per render event
    bool exclusive_wanted = false;
    bool exclusive = false; // the running stream is exclusive
};

} // namespace namx
