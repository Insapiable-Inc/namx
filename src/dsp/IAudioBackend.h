#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Common.h"

namespace namx
{

// Platform-agnostic audio backend interface.
//
// Linux  → JackBackend (full JACK client with graph routing).
// Windows → WasapiBackend (direct WASAPI client, no graph).
//
// AudioEngine owns one of these (chosen at runtime by IAudioBackend::create())
// and implements Callback to receive RT audio. MainWindow checks
// supports_port_routing() to know whether to expose the connection save/restore UI.
class IAudioBackend
{
public:
    // Real-time audio callback. Invoked on the backend's audio thread.
    // Buffer pointers are valid only for the duration of the call.
    class Callback
    {
    public:
        virtual ~Callback() = default;

        // Process one block. `in` is the mono input buffer, `outL`/`outR` the
        // stereo output buffers. All are `nframes` long.
        virtual void on_audio(const float* in, float* out_l, float* out_r, size_t nframes) = 0;

        // Notification that the audio buffer size has changed. Called on a
        // non-RT thread, serialised with on_audio. Implementations should
        // resize their work buffers here.
        virtual void on_buffer_size_changed(int /*newSize*/) {
        }
    };

    // Device info for device enumeration/selection.
    struct DeviceInfo
    {
        string id; // Stable device identifier (WASAPI endpoint ID, or "default")
        string name; // Human-readable name for the dropdown
    };

    // JACK-only port routing data. Empty on backends without a graph.
    struct PortConnections
    {
        vector<string> input_sources;
        vector<string> output_l_dests;
        vector<string> output_r_dests;
    };

    virtual ~IAudioBackend() = default;

    // Start the audio stream. `clientName` is the JACK client name where
    // applicable; ignored on backends that don't use it. `cb` must outlive
    // the backend (it's borrowed, not owned).
    virtual bool start(const string& client_name, Callback* cb) = 0;
    virtual void stop() = 0;

    virtual bool is_running() const = 0;
    virtual double get_sample_rate() const = 0;
    virtual int get_buffer_size() const = 0;

    // Short human-readable status string for the GUI pill,
    // e.g. "JACK ● 48000 Hz ● stereo out" or "WASAPI ● Speakers ● 48000 Hz".
    virtual string status_string() const = 0;

    // Buffer over/underruns since start or the last reset; -1 = the backend
    // doesn't report them. Safe to call from the GUI thread.
    virtual int xrun_count() const {
        return -1;
    }
    virtual void reset_xruns() {
    }

    // Capture + playback latency around our ports, in frames, as reported
    // by the backend (round trip = this; our own processing adds nothing
    // beyond the cycle it already includes). -1 = unknown.
    virtual int io_latency_frames() const {
        return -1;
    }

    // Return available capture (input) devices. Empty on JACK.
    virtual vector<DeviceInfo> enumerate_input_devices() const {
        return {};
    }

    // Return available render (output) devices. Empty on JACK.
    virtual vector<DeviceInfo> enumerate_output_devices() const {
        return {};
    }

    // Start audio using explicitly selected devices.
    // Empty or "default" ids fall back to system defaults.
    virtual bool start_with_devices(const string& client_name, Callback* cb,
                                    const string& input_device_id, const string& output_device_id) {
        // Default implementation ignores device ids.
        (void)input_device_id;
        (void)output_device_id;
        return start(client_name, cb);
    }

    // True if the backend exposes inter-app port routing (JACK only).
    virtual bool supports_port_routing() const {
        return false;
    }

    // Query the current port wiring. Empty if !supports_port_routing().
    virtual PortConnections current_connections() const {
        return {};
    }

    // Apply a saved wiring. No-op if !supports_port_routing().
    virtual void apply_connections(const PortConnections& /*c*/) {
    }

    // True if a port of that full name ("client:port") exists in the graph right now.
    // False if !supports_port_routing().
    virtual bool has_port(const string& /*name*/) const {
        return false;
    }

    // Exclusive device access (WASAPI): the app owns the device, picks its format and
    // the smallest period — the lowest latency, but no other app can use the device.
    // set_exclusive_mode() applies on the next start; if the device refuses, the
    // backend falls back to shared mode and exclusive_active() stays false.
    virtual bool supports_exclusive_mode() const {
        return false;
    }
    virtual void set_exclusive_mode(bool /*on*/) {
    }
    virtual bool exclusive_active() const {
        return false;
    }

    // Factory: returns the right backend for this build's target platform.
    // Defined in JackBackend.cpp (Linux) or WasapiBackend.cpp (Windows).
    static unique_ptr<IAudioBackend> create();
};

} // namespace namx
