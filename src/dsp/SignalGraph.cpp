#include "SignalGraph.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace namx
{

int input_count(NodeKind kind) {
    switch (kind) {
        case NodeKind::INPUT:
            return 0;
        case NodeKind::MONO:
        case NodeKind::STEREO:
            return 1;
        case NodeKind::OUTPUT:
        case NodeKind::MERGE:
            return 2;
    }
    return 0;
}

int output_count(NodeKind kind) {
    switch (kind) {
        case NodeKind::OUTPUT:
            return 0;
        case NodeKind::INPUT:
        case NodeKind::MONO:
        case NodeKind::MERGE:
            return 1;
        case NodeKind::STEREO:
            return 2;
    }
    return 0;
}

void MergeProcessor::mix_into(const float* a, const float* b, float* out, size_t n) {
    const float m = std::clamp(mix.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float mb = invert_b.load(std::memory_order_relaxed) ? -m : m;
    for (size_t i = 0; i < n; ++i) {
        out[i] = (1.0f - m) * a[i] + mb * b[i];
    }
}

void SelectProcessor::mix_into(const float* a, const float* b, float* out, size_t n) {
    const float target = use_b.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
    const float step = static_cast<float>(1000.0 / (FADE_MS * sample_rate));
    for (size_t i = 0; i < n; ++i) {
        fade = fade < target ? std::min(target, fade + step) : std::max(target, fade - step);
        out[i] = (1.0f - fade) * a[i] + fade * b[i];
    }
}

void GraphPlan::DelayLine::run(const float* in, float* out, size_t n, size_t delay) {
    constexpr size_t MASK = MAX_ALIGN - 1;
    delay = std::min(delay, MASK);
    for (size_t i = 0; i < n; ++i) {
        line[pos] = in[i];
        out[i] = line[(pos - delay) & MASK];
        pos = (pos + 1) & MASK;
    }
}

void GraphPlan::reserve(size_t frames) {
    if (frames <= capacity) {
        return;
    }
    for (auto& b : buffers) {
        b.assign(frames, 0.0f);
    }
    capacity = frames;
}

namespace
{

using NodeList = vector<std::shared_ptr<GraphNode>>;

// Index of each node ID in `nodes`; empty on a duplicate ID.
std::unordered_map<int, size_t> index_nodes(const NodeList& nodes) {
    std::unordered_map<int, size_t> index;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (!index.emplace(nodes[i]->id, i).second) {
            return {};
        }
    }
    return index;
}

// Ports in range and one wire per input; the error, or empty.
string check_wires(const NodeList& nodes, const std::unordered_map<int, size_t>& index,
                   const vector<GraphWire>& wires) {
    vector<std::pair<int, int>> taken;
    for (const auto& w : wires) {
        const auto from = index.find(w.from_node);
        const auto to = index.find(w.to_node);
        if (from == index.end() || to == index.end()) {
            return "a wire names a node that doesn't exist";
        }
        if (w.from_port < 0 || w.from_port >= output_count(nodes[from->second]->kind) ||
            w.to_port < 0 || w.to_port >= input_count(nodes[to->second]->kind)) {
            return "a wire names a port the node doesn't have";
        }
        const std::pair<int, int> target{w.to_node, w.to_port};
        if (std::find(taken.begin(), taken.end(), target) != taken.end()) {
            return "an input takes one wire (use a Merge node to join two signals)";
        }
        taken.push_back(target);
    }
    return {};
}

// Kahn's algorithm: node indices in processing order, or empty on a cycle.
vector<size_t> topological_order(const NodeList& nodes,
                                 const std::unordered_map<int, size_t>& index,
                                 const vector<GraphWire>& wires) {
    vector<int> pending(nodes.size(), 0);
    for (const auto& w : wires) {
        ++pending[index.at(w.to_node)];
    }
    vector<size_t> order;
    order.reserve(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (pending[i] == 0) {
            order.push_back(i);
        }
    }
    for (size_t k = 0; k < order.size(); ++k) {
        const int id = nodes[order[k]]->id;
        for (const auto& w : wires) {
            if (w.from_node == id && --pending[index.at(w.to_node)] == 0) {
                order.push_back(index.at(w.to_node));
            }
        }
    }
    if (order.size() != nodes.size()) {
        return {};
    }
    return order;
}

// Nodes on a path from the Input to the Output (the Input always: it feeds the input
// meter even with nothing wired). `order`: topological.
vector<bool> live_mask(const NodeList& nodes, const std::unordered_map<int, size_t>& index,
                       const vector<GraphWire>& wires, const vector<size_t>& order) {
    vector<bool> from_input(nodes.size(), false);
    for (const size_t i : order) {
        from_input[i] = from_input[i] || nodes[i]->kind == NodeKind::INPUT;
        if (from_input[i]) {
            for (const auto& w : wires) {
                if (w.from_node == nodes[i]->id) {
                    from_input[index.at(w.to_node)] = true;
                }
            }
        }
    }
    vector<bool> to_output(nodes.size(), false);
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const size_t i = *it;
        to_output[i] = to_output[i] || nodes[i]->kind == NodeKind::OUTPUT;
        if (to_output[i]) {
            for (const auto& w : wires) {
                if (w.to_node == nodes[i]->id) {
                    to_output[index.at(w.from_node)] = true;
                }
            }
        }
    }
    vector<bool> live(nodes.size(), false);
    for (size_t i = 0; i < nodes.size(); ++i) {
        live[i] = nodes[i]->kind == NodeKind::INPUT || (from_input[i] && to_output[i]);
    }
    return live;
}

} // namespace

vector<int> live_node_ids(const NodeList& nodes, const vector<GraphWire>& wires) {
    const auto index = index_nodes(nodes);
    if ((index.empty() && !nodes.empty()) || !check_wires(nodes, index, wires).empty()) {
        return {};
    }
    const vector<size_t> order = topological_order(nodes, index, wires);
    if (order.size() != nodes.size()) {
        return {};
    }
    const vector<bool> live = live_mask(nodes, index, wires, order);
    vector<int> ids;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (live[i]) {
            ids.push_back(nodes[i]->id);
        }
    }
    return ids;
}

vector<GraphWire> with_wire(const vector<GraphWire>& wires, const GraphWire& wire) {
    vector<GraphWire> next;
    next.reserve(wires.size() + 1);
    for (const auto& w : wires) {
        if (w.to_node != wire.to_node || w.to_port != wire.to_port) {
            next.push_back(w);
        }
    }
    next.push_back(wire);
    return next;
}

bool can_connect(const NodeList& nodes, const vector<GraphWire>& wires, const GraphWire& wire) {
    const auto index = index_nodes(nodes);
    if (index.empty() && !nodes.empty()) {
        return false;
    }
    const vector<GraphWire> next = with_wire(wires, wire);
    return check_wires(nodes, index, next).empty() &&
           !topological_order(nodes, index, next).empty();
}

bool can_add_wire(const NodeList& nodes, const vector<GraphWire>& wires, const GraphWire& wire) {
    const auto index = index_nodes(nodes);
    if (index.empty() && !nodes.empty()) {
        return false;
    }
    vector<GraphWire> next = wires;
    next.push_back(wire);
    return check_wires(nodes, index, next).empty() &&
           !topological_order(nodes, index, next).empty();
}

expected<unique_ptr<GraphPlan>, string>
compile_graph(const NodeList& nodes, const vector<GraphWire>& wires, size_t capacity) {
    const auto index = index_nodes(nodes);
    if (index.empty() && !nodes.empty()) {
        return std::unexpected(string("two nodes share an ID"));
    }
    if (const string err = check_wires(nodes, index, wires); !err.empty()) {
        return std::unexpected(err);
    }
    const vector<size_t> order = topological_order(nodes, index, wires);
    if (order.size() != nodes.size()) {
        return std::unexpected(string("the wires make a loop"));
    }

    const vector<bool> live = live_mask(nodes, index, wires, order);

    auto plan = make_unique<GraphPlan>();
    plan->nodes = nodes;
    plan->buffers.emplace_back(); // SILENCE
    plan->buffers.emplace_back(); // SCRATCH
    std::unordered_map<int, array<int, 2>> out_buffers; // node ID → buffer per output port

    for (const size_t i : order) {
        if (!live[i]) {
            continue;
        }
        GraphNode* node = nodes[i].get();
        GraphPlan::Step step{
            node, {GraphPlan::SILENCE, GraphPlan::SILENCE}, {-1, -1}, {false, false}};
        for (const auto& w : wires) {
            if (w.to_node != node->id || !live[index.at(w.from_node)]) {
                continue;
            }
            step.in[w.to_port] = out_buffers.at(w.from_node)[static_cast<size_t>(w.from_port)];
            step.in_wired[w.to_port] = true;
        }
        array<int, 2> outs{GraphPlan::SILENCE, GraphPlan::SILENCE};
        for (int p = 0; p < output_count(node->kind); ++p) {
            outs[static_cast<size_t>(p)] = static_cast<int>(plan->buffers.size());
            step.out[p] = outs[static_cast<size_t>(p)];
            plan->buffers.emplace_back();
        }
        if (node->kind == NodeKind::MONO) {
            step.out[1] = GraphPlan::SCRATCH; // EffectBase::process writes its copy of L there
        }
        // Two signals meet: a delay line and an aligned buffer per input (the Output's
        // aligned buffers are what the engine plays).
        const bool both_wired = step.in_wired[0] && step.in_wired[1];
        if (node->kind == NodeKind::MERGE || (node->kind == NodeKind::OUTPUT && both_wired)) {
            step.align = static_cast<int>(plan->lines.size());
            for (int p = 0; p < 2; ++p) {
                plan->lines.emplace_back();
                step.aligned[p] = static_cast<int>(plan->buffers.size());
                plan->buffers.emplace_back();
                if (node->kind == NodeKind::OUTPUT) {
                    step.out[p] = step.aligned[p];
                }
            }
        }
        out_buffers[node->id] = outs;

        if (node->kind == NodeKind::INPUT) {
            plan->input_buffer = outs[0];
            plan->input = static_cast<IoProcessor*>(&node->processor());
        } else if (node->kind == NodeKind::OUTPUT) {
            plan->output = static_cast<IoProcessor*>(&node->processor());
            if (both_wired) {
                plan->out_buffer[0] = step.aligned[0];
                plan->out_buffer[1] = step.aligned[1];
            } else { // one side wired = mono out on both
                plan->out_buffer[0] = step.in_wired[0] ? step.in[0] : step.in[1];
                plan->out_buffer[1] = step.in_wired[1] ? step.in[1] : step.in[0];
            }
        }
        plan->steps.push_back(step);
    }
    // An Output nothing reaches still carries the gain and limiter settings.
    if (!plan->output) {
        for (const auto& n : nodes) {
            if (n->kind == NodeKind::OUTPUT) {
                plan->output = static_cast<IoProcessor*>(&n->processor());
            }
        }
    }
    plan->latency.assign(plan->buffers.size(), 0.0);
    plan->reserve(capacity);
    return plan;
}

void run_plan(GraphPlan& plan, size_t n) {
    auto buf = [&plan](int i) {
        return plan.buffers[static_cast<size_t>(i)].data();
    };
    auto latency = [&plan](int i) -> double& {
        return plan.latency[static_cast<size_t>(i)];
    };
    for (auto& step : plan.steps) {
        EffectBase& fx = step.node->processor();
        fx.consume_pending_reset();
        // Line the inputs up: the earlier one waits for the later.
        double in_latency = latency(step.in[0]);
        if (step.align >= 0) {
            in_latency = std::max(in_latency, latency(step.in[1]));
            for (int p = 0; p < 2; ++p) {
                const double wait = in_latency - latency(step.in[p]);
                plan.lines[static_cast<size_t>(step.align + p)].run(
                    buf(step.in[p]), buf(step.aligned[p]), n,
                    static_cast<size_t>(std::lround(wait)));
            }
        }
        const double own = fx.is_enabled() ? fx.latency_ms() * fx.get_sample_rate() / 1000.0 : 0.0;
        for (const int o : step.out) {
            if (o > GraphPlan::SCRATCH) {
                latency(o) = in_latency + own;
            }
        }
        switch (step.node->kind) {
            case NodeKind::INPUT:
            case NodeKind::OUTPUT:
                break;
            case NodeKind::MONO: {
                float* l = buf(step.out[0]);
                std::copy_n(buf(step.in[0]), n, l);
                fx.process(l, buf(step.out[1]), n, false);
                break;
            }
            case NodeKind::STEREO: {
                float* l = buf(step.out[0]);
                float* r = buf(step.out[1]);
                // Mono in → the effect's stereo out (bypassed: the input on both).
                std::copy_n(buf(step.in[0]), n, l);
                std::copy_n(buf(step.in[0]), n, r);
                fx.process(l, r, n, false);
                break;
            }
            case NodeKind::MERGE: {
                float* out = buf(step.out[0]);
                if (fx.is_enabled()) {
                    static_cast<TwoInputProcessor&>(fx).mix_into(buf(step.aligned[0]),
                                                                 buf(step.aligned[1]), out, n);
                } else {
                    std::copy_n(buf(step.aligned[0]), n, out);
                }
                break;
            }
        }
    }
}

} // namespace namx
