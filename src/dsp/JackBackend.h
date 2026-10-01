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

    bool start(const std::string& client_name, Callback* cb) override;
    void stop() override;

    bool is_running() const override {
        return m_running.load();
    }
    double sample_rate() const override {
        return m_sample_rate;
    }
    int buffer_size() const override {
        return m_buffer_size;
    }
    std::string status_string() const override;

    bool supports_port_routing() const override {
        return true;
    }
    PortConnections current_connections() const override;
    void apply_connections(const PortConnections& c) override;

    int xrun_count() const override {
        return m_xruns.load(std::memory_order_relaxed);
    }
    void reset_xruns() override {
        m_xruns.store(0, std::memory_order_relaxed);
    }
    int io_latency_frames() const override;

    std::vector<DeviceInfo> enumerate_input_devices() const override;
    std::vector<DeviceInfo> enumerate_output_devices() const override;

private:
    static int jack_process_callback(jack_nframes_t nframes, void* arg);
    static int jack_buffer_size_callback(jack_nframes_t nframes, void* arg);
    static void jack_shutdown_callback(void* arg);
    static int jack_xrun_callback(void* arg);

    int process_audio(jack_nframes_t nframes);
    int on_buffer_size(jack_nframes_t nframes);

    jack_client_t* m_client = nullptr;
    jack_port_t* m_port_in = nullptr;
    jack_port_t* m_port_out_l = nullptr;
    jack_port_t* m_port_out_r = nullptr;

    Callback* m_callback = nullptr;
    std::atomic<bool> m_running{false};
    std::atomic<int> m_xruns{0};
    double m_sample_rate = 44100.0;
    int m_buffer_size = 0;
};
