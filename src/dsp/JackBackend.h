#pragma once

#include <atomic>

#include <jack/jack.h>

#include "Common.h"
#include "IAudioBackend.h"

namespace namx
{

// JACK implementation of IAudioBackend.
// All JACK-specific state (client handle, ports, callbacks) lives here so
// AudioEngine doesn't have to include <jack/jack.h>.
class JackBackend : public IAudioBackend
{
public:
    JackBackend();
    ~JackBackend() override;

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

    bool supports_port_routing() const override {
        return true;
    }
    PortConnections current_connections() const override;
    void apply_connections(const PortConnections& c) override;
    bool has_port(const string& name) const override;

    int xrun_count() const override {
        return xruns.load(std::memory_order_relaxed);
    }
    void reset_xruns() override {
        xruns.store(0, std::memory_order_relaxed);
    }
    int io_latency_frames() const override;

    vector<DeviceInfo> enumerate_input_devices() const override;
    vector<DeviceInfo> enumerate_output_devices() const override;

private:
    static int jack_process_callback(jack_nframes_t nframes, void* arg);
    static int jack_buffer_size_callback(jack_nframes_t nframes, void* arg);
    static void jack_shutdown_callback(void* arg);
    static int jack_xrun_callback(void* arg);

    int process_audio(jack_nframes_t nframes);
    // Drops every connection of one of our ports.
    void disconnect_all(jack_port_t* p);
    int on_buffer_size(jack_nframes_t nframes);

    jack_client_t* client = nullptr;
    jack_port_t* port_in = nullptr;
    jack_port_t* port_out_l = nullptr;
    jack_port_t* port_out_r = nullptr;

    Callback* callback = nullptr;
    atomic<bool> running{false};
    atomic<int> xruns{0};
    double sample_rate = 44100.0;
    // Written on JACK's notification thread, read by the GUI's status timer.
    atomic<int> buffer_size{0};
};

} // namespace namx
