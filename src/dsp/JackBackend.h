#pragma once

#include <atomic>

#include <jack/jack.h>

#include "IAudioBackend.h"

/// JACK implementation of IAudioBackend.
/// All JACK-specific state (client handle, ports, callbacks) lives here so
/// AudioEngine doesn't have to include <jack/jack.h>.
class JackBackend : public IAudioBackend
{
public:
    JackBackend();
    ~JackBackend() override;

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

    bool supportsPortRouting() const override {
        return true;
    }
    PortConnections currentConnections() const override;
    void applyConnections(const PortConnections& c) override;

    int xrunCount() const override {
        return mXruns.load(std::memory_order_relaxed);
    }
    void resetXruns() override {
        mXruns.store(0, std::memory_order_relaxed);
    }
    int ioLatencyFrames() const override;

    std::vector<DeviceInfo> enumerateInputDevices() const override;
    std::vector<DeviceInfo> enumerateOutputDevices() const override;

private:
    static int jackProcessCallback(jack_nframes_t nframes, void* arg);
    static int jackBufferSizeCallback(jack_nframes_t nframes, void* arg);
    static void jackShutdownCallback(void* arg);
    static int jackXrunCallback(void* arg);

    int processAudio(jack_nframes_t nframes);
    int onBufferSize(jack_nframes_t nframes);

    jack_client_t* mClient = nullptr;
    jack_port_t* mPortIn = nullptr;
    jack_port_t* mPortOutL = nullptr;
    jack_port_t* mPortOutR = nullptr;

    Callback* mCallback = nullptr;
    std::atomic<bool> mRunning{false};
    std::atomic<int> mXruns{0};
    double mSampleRate = 44100.0;
    int mBufferSize = 0;
};
