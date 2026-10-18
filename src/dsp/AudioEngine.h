#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "Common.h"
#include "IAudioBackend.h"
#include "Metronome.h"
#include "OutputLimiter.h"
#include "RtSlot.h"
#include "SignalGraph.h"
#include "TunerProcessor.h"

namespace namx
{

// Platform-agnostic audio engine. Runs the signal graph (SignalGraph.h), then the
// metronome and the output limiter. Delegates I/O to an IAudioBackend (JACK on Linux, WASAPI on
// Windows) created via IAudioBackend::create().
class AudioEngine : public IAudioBackend::Callback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    bool start(const string& client_name = "NAM-X");
    void stop();

    bool start_with_devices(const string& input_device_id, const string& output_device_id,
                            const string& client_name = "NAM-X");

    // Exclusive device access (see IAudioBackend); applies on the next start.
    bool supports_exclusive_mode() const;
    void set_exclusive_mode(bool on) {
        exclusive_wanted = on;
    }
    bool is_exclusive_wanted() const {
        return exclusive_wanted;
    }
    bool exclusive_active() const {
        return backend && backend->exclusive_active();
    }

    bool is_running() const {
        return backend && backend->is_running();
    }
    double sample_rate() const {
        return backend ? backend->get_sample_rate() : 44100.0;
    }
    int buffer_size() const {
        return backend ? backend->get_buffer_size() : 0;
    }

    // Human-readable status, e.g. "JACK ● 48000 Hz ● stereo out".
    string backend_status() const {
        return backend ? backend->status_string() : string{};
    }
    bool supports_port_routing() const {
        return backend && backend->supports_port_routing();
    }

    // Device enumeration (creates temp backend, so works before start())
    vector<IAudioBackend::DeviceInfo> enumerate_input_devices() const;
    vector<IAudioBackend::DeviceInfo> enumerate_output_devices() const;

    // Signal graph (ui/node-graph)
    // A new node of a NodeCatalog type, configured for the running stream (sample
    // rate, block size) but not yet live: hand it to set_graph to make it run.
    std::shared_ptr<GraphNode> create_node(int id, const string& type, bool stereo);
    // Compiles and publishes the graph RT-safely (RtSlot, like model swaps): nodes
    // that drop out stay alive until the audio thread has left the old plan. Nodes
    // whose input wiring changed get a reset. On an error nothing changes.
    expected<void, string> set_graph(const vector<std::shared_ptr<GraphNode>>& nodes,
                                     const vector<GraphWire>& wires);

    // Frees plans, models and IRs the audio thread has provably left (all of them
    // when it is stopped). Edits do it themselves; the GUI also polls it, so a removed
    // node's model doesn't wait for the next edit. GUI thread.
    void reclaim_retired();

    // Model / IR of a node ("nam" / "ir" nodes); retired objects are reclaimed.
    bool load_model(GraphNode& node, const string& path);
    bool load_ir(GraphNode& node, const string& path);

    // Global, outside the graph: reads the guitar after the input gain; while on with
    // Mute set it silences what enters the graph.
    TunerProcessor& get_tuner() {
        return tuner;
    }

    // Mixed in after the output gain — outside the graph, not metered.
    Metronome& get_metronome() {
        return metronome;
    }
    OutputLimiter& get_limiter() {
        return limiter;
    }

    // Port-routing pass-through (no-ops on backends without it)
    using PortConnections = IAudioBackend::PortConnections;
    PortConnections current_connections() const {
        return backend ? backend->current_connections() : PortConnections{};
    }
    void apply_connections(const PortConnections& c) {
        if (backend) {
            backend->apply_connections(c);
        }
    }

    // VU meters
    // The audio thread accumulates the peak (max) until the GUI takes it;
    // take-and-reset so no transient between polls is ever missed.
    float take_input_peak() {
        return input_peak.exchange(0.0f);
    }
    // The output is stereo: one peak per channel (0 = left, 1 = right).
    float take_output_peak(int channel) {
        return output_peak[channel].exchange(0.0f);
    }

    // Diagnostics (status bar)
    // Highest audio-thread time per block since the last take, as a
    // fraction of the block's duration (1.0 = the whole budget).
    float take_dsp_load_peak() {
        return dsp_load_peak.exchange(0.0f);
    }
    int xrun_count() const {
        return backend ? backend->xrun_count() : -1;
    }
    // Blocks in which a non-finite sample was scrubbed since the last take
    // (process_block step 5).
    int take_non_finite_blocks() {
        return non_finite_blocks.exchange(0, std::memory_order_relaxed);
    }
    void reset_xruns() {
        if (backend) {
            backend->reset_xruns();
        }
    }
    // Processing latency the graph adds right now: the slowest path from the Input to
    // the Output (each node on it with a nonzero latency, by type name) plus the
    // output limiter. GUI thread.
    struct LatencySource
    {
        string name;
        double ms;
    };
    vector<LatencySource> processing_latency() const;

    int io_latency_frames() const {
        return backend ? backend->io_latency_frames() : -1;
    }

    // IAudioBackend::Callback
    void on_audio(const float* in, float* out_l, float* out_r, size_t nframes) override;
    void on_buffer_size_changed(int new_size) override;

private:
    unique_ptr<IAudioBackend> backend;
    bool exclusive_wanted = false; // GUI thread; handed to each new backend

    // The compiled graph — published through an RtSlot. The audio thread binds
    // one plan per callback; set_graph() publishes a new one and the old plan
    // (and any node only it still holds) is freed once the audio thread left it.
    RtSlot<GraphPlan> plan_slot;
    vector<GraphWire> live_wires; // GUI thread: the wiring of the published plan

    // Sample rate, block size and plan publishing are serialized: a node created or
    // published while the backend reconfigures still gets the new settings.
    std::mutex config_mutex;
    size_t max_block{512};

    TunerProcessor tuner;
    OutputLimiter limiter;
    Metronome metronome;

    atomic<float> input_peak{0.0f};
    atomic<float> output_peak[2]{0.0f, 0.0f}; // left, right
    atomic<float> dsp_load_peak{0.0f};
    atomic<int> non_finite_blocks{0};

    // on_audio = timing around this.
    void process_block(const float* in, float* out_l, float* out_r, size_t nframes);

    // Set from on_buffer_size_changed (never concurrent with the audio thread).
    double chain_sample_rate{0.0};
    float smooth_coeff{0.999f}; // recomputed per sample rate for a ~20 ms time constant

    static inline float db_to_linear(float db) {
        return std::pow(10.0f, db / 20.0f);
    }
};

} // namespace namx
