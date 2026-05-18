#pragma once

#include <memory>
#include <string>
#include <vector>

/// Platform-agnostic audio backend interface.
///
/// Linux  → JackBackend (full JACK client with graph routing).
/// Windows → WasapiBackend (direct WASAPI client, no graph).
///
/// AudioEngine owns one of these (chosen at runtime by IAudioBackend::create())
/// and implements Callback to receive RT audio. MainWindow checks
/// supportsPortRouting() to know whether to expose the connection save/restore UI.
class IAudioBackend
{
public:
    /// Real-time audio callback. Invoked on the backend's audio thread.
    /// Buffer pointers are valid only for the duration of the call.
    class Callback
    {
    public:
        virtual ~Callback() = default;

        /// Process one block. `in` is the mono input buffer, `outL`/`outR` the
        /// stereo output buffers. All are `nframes` long.
        virtual void onAudio(const float* in, float* outL, float* outR, int nframes) = 0;

        /// Notification that the audio buffer size has changed. Called on a
        /// non-RT thread, serialised with onAudio. Implementations should
        /// resize their work buffers here.
        virtual void onBufferSizeChanged(int /*newSize*/) {}
    };

    /// Device info for device enumeration/selection.
    struct DeviceInfo
    {
        std::string id;    // Stable device identifier (WASAPI endpoint ID, or "default")
        std::string name;  // Human-readable name for the dropdown
    };

    /// JACK-only port routing data. Empty on backends without a graph.
    struct PortConnections
    {
        std::vector<std::string> inputSources;
        std::vector<std::string> outputLDests;
        std::vector<std::string> outputRDests;
    };

    virtual ~IAudioBackend() = default;

    /// Start the audio stream. `clientName` is the JACK client name where
    /// applicable; ignored on backends that don't use it. `cb` must outlive
    /// the backend (it's borrowed, not owned).
    virtual bool start(const std::string& clientName, Callback* cb) = 0;
    virtual void stop() = 0;

    virtual bool   isRunning()  const = 0;
    virtual double sampleRate() const = 0;
    virtual int    bufferSize() const = 0;

    /// Short human-readable status string for the GUI pill,
    /// e.g. "JACK ● 48000 Hz ● stereo out" or "WASAPI ● Speakers ● 48000 Hz".
    virtual std::string statusString() const = 0;

    /// Return available capture (input) devices. Empty on JACK.
    virtual std::vector<DeviceInfo> enumerateInputDevices() const { return {}; }

    /// Return available render (output) devices. Empty on JACK.
    virtual std::vector<DeviceInfo> enumerateOutputDevices() const { return {}; }

    /// Start audio using explicitly selected devices.
    /// Empty or "default" ids fall back to system defaults.
    virtual bool startWithDevices(const std::string& clientName, Callback* cb,
                                  const std::string& inputDeviceId,
                                  const std::string& outputDeviceId)
    {
        // Default implementation ignores device ids.
        (void)inputDeviceId; (void)outputDeviceId;
        return start(clientName, cb);
    }

    /// True if the backend exposes inter-app port routing (JACK only).
    virtual bool supportsPortRouting() const { return false; }

    /// Query the current port wiring. Empty if !supportsPortRouting().
    virtual PortConnections currentConnections() const { return {}; }

    /// Apply a saved wiring. No-op if !supportsPortRouting().
    virtual void applyConnections(const PortConnections& /*c*/) {}

    /// Factory: returns the right backend for this build's target platform.
    /// Defined in JackBackend.cpp (Linux) or WasapiBackend.cpp (Windows).
    static std::unique_ptr<IAudioBackend> create();
};
