#include "AudioEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>

#include <NAM/activations.h>

#include "Common.h"
#include "IRProcessor.h"
#include "Log.h"
#include "NamProcessor.h"
#include "NodeCatalog.h"

namespace namx
{

AudioEngine::AudioEngine() {
    // NAM's fast tanh approximation: ~2.4× on Tanh WaveNets (the common
    // architecture), inaudible — the official plugin enables it too. Global
    // to the NAM core, so it covers every model loaded later.
    nam::activations::Activation::enable_fast_tanh();
}

void AudioEngine::reclaim_retired() {
    // Only valid with no audio callback possible: after stop(), or before
    // the backend was ever started.
    const bool stopped = !is_running();
    plan_slot.reclaim(stopped);
    if (const GraphPlan* plan = plan_slot.current()) {
        for (const auto& node : plan->nodes) {
            if (node->type == "nam") {
                static_cast<NamProcessor&>(node->processor()).reclaim_retired(stopped);
            } else if (node->type == "ir") {
                static_cast<IRProcessor&>(node->processor()).reclaim_retired(stopped);
            }
        }
    }
}

std::shared_ptr<GraphNode> AudioEngine::create_node(int id, const string& type, bool stereo) {
    std::shared_ptr<GraphNode> node = NodeCatalog::create(id, type, stereo);
    if (!node) {
        return nullptr;
    }
    const std::lock_guard<std::mutex> lock(config_mutex);
    if (chain_sample_rate > 0.0) {
        node->processor().set_sample_rate(chain_sample_rate);
    }
    if (node->type == "nam") {
        static_cast<NamProcessor&>(node->processor()).prepare_buffer(max_block);
    }
    return node;
}

bool AudioEngine::load_model(GraphNode& node, const string& path) {
    if (node.type != "nam") {
        return false;
    }
    const bool ok = static_cast<NamProcessor&>(node.processor()).load_model(path);
    reclaim_retired();
    return ok;
}

bool AudioEngine::load_ir(GraphNode& node, const string& path) {
    if (node.type != "ir") {
        return false;
    }
    const bool ok = static_cast<IRProcessor&>(node.processor()).load(path);
    reclaim_retired();
    return ok;
}

expected<void, string> AudioEngine::set_graph(const vector<std::shared_ptr<GraphNode>>& nodes,
                                              const vector<GraphWire>& wires) {
    {
        const std::lock_guard<std::mutex> lock(config_mutex);
        auto plan = compile_graph(nodes, wires, std::max<size_t>(max_block, 512));
        if (!plan) {
            return std::unexpected(plan.error());
        }
        // A node created before the stream (re)configured missed the new rate; only
        // nodes that aren't live yet can, so this never touches a running one.
        for (const auto& node : nodes) {
            if (chain_sample_rate > 0.0 &&
                node->processor().get_sample_rate() != chain_sample_rate) {
                node->processor().set_sample_rate(chain_sample_rate);
                if (node->type == "nam") {
                    static_cast<NamProcessor&>(node->processor()).prepare_buffer(max_block);
                }
            }
        }
        // A node fed from somewhere new carries state from its old source (tails,
        // diverged channels): have the audio thread clear it.
        for (const auto& node : nodes) {
            auto feeds = [&node](const vector<GraphWire>& list) {
                vector<GraphWire> in;
                for (const auto& w : list) {
                    if (w.to_node == node->id) {
                        in.push_back(w);
                    }
                }
                std::sort(in.begin(), in.end(), [](const GraphWire& a, const GraphWire& b) {
                    return a.to_port < b.to_port;
                });
                return in;
            };
            if (feeds(wires) != feeds(live_wires)) {
                node->processor().request_reset();
            }
        }
        plan_slot.publish(std::move(*plan));
        live_wires = wires;
    }
    reclaim_retired();
    return {};
}

vector<AudioEngine::LatencySource> AudioEngine::processing_latency() const {
    vector<LatencySource> out;
    const GraphPlan* plan = plan_slot.current();
    if (plan) {
        // Per buffer: the slowest path's latency into it, and that path's nodes.
        struct Path
        {
            double ms = 0.0;
            vector<const GraphNode*> nodes;
        };
        vector<Path> paths(plan->buffers.size());
        for (const auto& step : plan->steps) {
            Path in = paths[static_cast<size_t>(step.in[0])];
            if (input_count(step.node->kind) > 1 &&
                paths[static_cast<size_t>(step.in[1])].ms > in.ms) {
                in = paths[static_cast<size_t>(step.in[1])];
            }
            const EffectBase& fx = step.node->processor();
            const double own = fx.is_enabled() ? fx.latency_ms() : 0.0;
            if (own > 0.0) {
                in.ms += own;
                in.nodes.push_back(step.node);
            }
            for (const int o : step.out) {
                if (o > 1) { // not silence or the scratch buffer
                    paths[static_cast<size_t>(o)] = in;
                }
            }
        }
        const Path& l = paths[static_cast<size_t>(plan->out_buffer[0])];
        const Path& r = paths[static_cast<size_t>(plan->out_buffer[1])];
        for (const GraphNode* node : (r.ms > l.ms ? r : l).nodes) {
            const NodeType* type = NodeCatalog::find(node->type);
            out.push_back({type ? type->name : node->type, node->processor().latency_ms()});
        }
    }
    if (const double ms = limiter.latency_ms(); ms > 0.0) {
        out.push_back({"Limiter", ms});
    }
    return out;
}

AudioEngine::~AudioEngine() {
    stop();
}

bool AudioEngine::start(const string& client_name) {
    if (backend && backend->is_running()) {
        return true;
    }

    backend = IAudioBackend::create();
    if (!backend) {
        qCWarning(lc_audio, "[AudioEngine] No audio backend available for this platform");
        return false;
    }
    backend->set_exclusive_mode(exclusive_wanted);

    // Sample rate and buffer size reach the nodes via on_buffer_size_changed,
    // which the backend invokes BEFORE activating the stream — configuring
    // them after activation would race the audio thread.
    if (!backend->start(client_name, this)) {
        backend.reset();
        return false;
    }

    return true;
}

bool AudioEngine::start_with_devices(const string& input_device_id, const string& output_device_id,
                                     const string& client_name) {
    stop();

    backend = IAudioBackend::create();
    if (!backend) {
        qCWarning(lc_audio, "[AudioEngine] No audio backend available for this platform");
        return false;
    }
    backend->set_exclusive_mode(exclusive_wanted);

    if (!backend->start_with_devices(client_name, this, input_device_id, output_device_id)) {
        backend.reset();
        return false;
    }

    return true;
}

void AudioEngine::stop() {
    if (backend) {
        backend->stop();
        backend.reset();
    }
    // No callback can run now — drop everything retired while it was live.
    reclaim_retired();
}

bool AudioEngine::supports_exclusive_mode() const {
    if (backend) {
        return backend->supports_exclusive_mode();
    }
    const unique_ptr<IAudioBackend> temp = IAudioBackend::create();
    return temp && temp->supports_exclusive_mode();
}

vector<IAudioBackend::DeviceInfo> AudioEngine::enumerate_input_devices() const {
    unique_ptr<IAudioBackend> temp = IAudioBackend::create();
    return temp ? temp->enumerate_input_devices() : vector<IAudioBackend::DeviceInfo>{};
}

vector<IAudioBackend::DeviceInfo> AudioEngine::enumerate_output_devices() const {
    unique_ptr<IAudioBackend> temp = IAudioBackend::create();
    return temp ? temp->enumerate_output_devices() : vector<IAudioBackend::DeviceInfo>{};
}

// IAudioBackend::Callback

void AudioEngine::on_buffer_size_changed(int new_size) {
    if (new_size <= 0) {
        return;
    }
    const size_t n = static_cast<size_t>(new_size); // backends report int; positive here

    // Backends call this before activating the stream, and with processing
    // suspended on later changes — the one safe point to reconfigure live nodes.
    const std::lock_guard<std::mutex> lock(config_mutex);
    max_block = std::max(max_block, n);
    GraphPlan* plan = plan_slot.current();
    const double sr = backend ? backend->get_sample_rate() : 0.0;
    if (sr > 0.0 && sr != chain_sample_rate) {
        chain_sample_rate = sr;
        if (plan) {
            for (const auto& node : plan->nodes) {
                node->processor().set_sample_rate(sr);
            }
        }
        tuner.set_sample_rate(sr);
        metronome.set_sample_rate(sr);
        limiter.set_sample_rate(sr);
        // ~20 ms one-pole gain smoothing, independent of sample rate.
        smooth_coeff = std::exp(-1.0f / (0.020f * static_cast<float>(sr)));
    }
    if (plan) {
        plan->reserve(max_block);
        // After the rate, so a re-Reset() picks it up.
        for (const auto& node : plan->nodes) {
            if (node->type == "nam") {
                static_cast<NamProcessor&>(node->processor()).prepare_buffer(max_block);
            }
        }
    }
}

void AudioEngine::on_audio(const float* in, float* out_l, float* out_r, size_t n) {
    // DSP load: wall time of the block vs its real-time duration. steady_clock
    // is a vDSO read — no syscall, RT-safe.
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    process_block(in, out_l, out_r, n);
    const double secs =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (n > 0 && chain_sample_rate > 0.0) {
        const float load = static_cast<float>(secs * chain_sample_rate / static_cast<double>(n));
        if (load > dsp_load_peak.load(std::memory_order_relaxed)) {
            dsp_load_peak.store(load, std::memory_order_relaxed);
        }
    }
}

void AudioEngine::process_block(const float* in, float* out_l, float* out_r, size_t n) {
    // Rewire fence: every exit counts the block so set_graph() knows when a
    // retired plan is safe to free.
    struct BlockGuard
    {
        RtSlot<GraphPlan>& s;
        ~BlockGuard() {
            s.end_block();
        }
    } guard{plan_slot};

    GraphPlan* plan = plan_slot.acquire();
    // No graph yet, or a block larger than the plan was sized for: silence rather
    // than an overrun.
    if (!plan || n > plan->capacity) {
        std::fill_n(out_l, n, 0.0f);
        std::fill_n(out_r, n, 0.0f);
        return;
    }

    // 1. The Input node: input gain (smoothed) into its buffer.
    float in_peak = 0.0f;
    if (plan->input_buffer >= 0) {
        float* buf = plan->buffers[static_cast<size_t>(plan->input_buffer)].data();
        const float target = db_to_linear(plan->input->get_gain_db());
        float& gain = plan->input->smoothed;
        for (size_t i = 0; i < n; ++i) {
            gain = smooth_coeff * gain + (1.0f - smooth_coeff) * target;
            buf[i] = in[i] * gain;
            in_peak = std::max(in_peak, std::abs(buf[i]));
        }
    }
    // The tuner (off = a no-op): reads the guitar, and with Mute on silences the graph's
    // input. Its R copy goes to the plan's scratch buffer.
    if (plan->input_buffer >= 0) {
        tuner.process(plan->buffers[static_cast<size_t>(plan->input_buffer)].data(),
                      plan->buffers[GraphPlan::SCRATCH].data(), n, false);
    }
    // Peak-hold: accumulate the max until the GUI takes and resets it, so the
    // poll can't miss transients between blocks.
    if (in_peak > input_peak.load(std::memory_order_relaxed)) {
        input_peak.store(in_peak, std::memory_order_relaxed);
    }

    // 2. The graph.
    run_plan(*plan, n);

    // 3. Output gain (smoothed) + write to output buffers.
    const float* src_l = plan->buffers[static_cast<size_t>(plan->out_buffer[0])].data();
    const float* src_r = plan->buffers[static_cast<size_t>(plan->out_buffer[1])].data();
    const float target_out = plan->output ? db_to_linear(plan->output->get_gain_db()) : 1.0f;
    float out_gain_scratch = 1.0f;
    float& out_gain = plan->output ? plan->output->smoothed : out_gain_scratch;
    for (size_t i = 0; i < n; ++i) {
        out_gain = smooth_coeff * out_gain + (1.0f - smooth_coeff) * target_out;
        out_l[i] = src_l[i] * out_gain;
        out_r[i] = src_r[i] * out_gain;
    }

    // 4. The output meter: the guitar at the output stage, before the click is
    //    added and before the limiter — red means the limiter is (or would be)
    //    catching peaks.
    float peak_l = 0.0f;
    float peak_r = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        peak_l = std::max(peak_l, std::abs(out_l[i]));
        peak_r = std::max(peak_r, std::abs(out_r[i]));
    }
    if (peak_l > output_peak[0].load(std::memory_order_relaxed)) {
        output_peak[0].store(peak_l, std::memory_order_relaxed);
    }
    if (peak_r > output_peak[1].load(std::memory_order_relaxed)) {
        output_peak[1].store(peak_r, std::memory_order_relaxed);
    }

    // 5. A non-finite sample — from the capture port, a model overflowing in
    //    inference, an effect — would latch in every feedback path (delay lines,
    //    reverb tanks, biquads) and pass the limiter to the hardware (audit
    //    2026-10-04, Fable item 03). Scrub it, reset every node once, and count it
    //    for the status bar.
    bool finite = true;
    for (size_t i = 0; i < n; ++i) {
        finite = finite && std::isfinite(out_l[i]) && std::isfinite(out_r[i]);
    }
    if (!finite) {
        for (size_t i = 0; i < n; ++i) {
            out_l[i] = std::isfinite(out_l[i]) ? out_l[i] : 0.0f;
            out_r[i] = std::isfinite(out_r[i]) ? out_r[i] : 0.0f;
        }
        for (auto& step : plan->steps) {
            step.node->processor().request_reset();
        }
        non_finite_blocks.fetch_add(1, std::memory_order_relaxed);
    }

    // 6. Metronome (own volume/pan), then the limiter (look-ahead; a no-op when
    //    off) as the very last stage, so it guards everything that leaves the app —
    //    guitar plus click (review item 27).
    limiter.set_enabled(plan->output && plan->output->get_limiter());
    metronome.process(out_l, out_r, n);
    limiter.process(out_l, out_r, n);
}

} // namespace namx
