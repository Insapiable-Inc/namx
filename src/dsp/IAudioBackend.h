#pragma once

#include <memory>
#include <string>
#include <vector>

/// Platform-agnostic audio backend interface.
///
/// On Linux this is implemented by JackBackend (full JACK client with port
/// graph routing). On Windows the implementation is WasapiBackend, which
/// drives audio directly through WASAPI and has no concept of a graph.
///
/// AudioEngine owns one of these and implements Callback to receive RT audio.
/// MainWindow checks supportsPortRouting() to know whether to expose the
/// JACK-specific connection save/restore UI.
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
        /// resize their work buffers here. Optional override.
        virtual void onBufferSizeChanged(int /*newSize*/) {}
    };

    /// JACK-only port routing data. Empty on backends that don't have a graph
    /// (e.g. WASAPI). Kept on the interface so MainWindow can save/restore
    /// generically; non-JACK backends just return empty / ignore.
    struct PortConnections
    {
        std::vector<std::string> inputSources;
        std::vector<std::string> outputLDests;
        std::vector<std::string> outputRDests;
    };

    virtual ~IAudioBackend() = default;

    /// Start the audio stream. `clientName` is used as the JACK client name
    /// where applicable; ignored otherwise. `cb` must outlive the backend.
    virtual bool start(const std::string& clientName, Callback* cb) = 0;
    virtual void stop() = 0;

    virtual bool   isRunning()  const = 0;
    virtual double sampleRate() const = 0;
    virtual int    bufferSize() const = 0;

    /// Short human-readable status string, e.g. "JACK ● 48000 Hz ● stereo out"
    /// or "WASAPI ● Speakers (Realtek) ● 48000 Hz". Shown in the GUI pill.
    virtual std::string statusString() const = 0;

    /// True if this backend supports inter-app port routing (i.e. JACK).
    /// False for device-based backends (WASAPI, ASIO, etc.).
    virtual bool supportsPortRouting() const { return false; }

    /// Query the current port wiring. Empty if !supportsPortRouting().
    virtual PortConnections currentConnections() const { return {}; }

    /// Replace the port wiring with the saved set. No-op if !supportsPortRouting().
    virtual void applyConnections(const PortConnections& /*c*/) {}

    /// Factory: returns the right backend for this build's target platform.
    static std::unique_ptr<IAudioBackend> create();
};
