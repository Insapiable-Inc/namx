#include "SignalGraph.h"

#include <algorithm>
#include <unordered_map>

namespace namx
{

int input_count(NodeKind kind) {
    switch (kind) {
        case NodeKind::INPUT:
            return 0;
        case NodeKind::MONO:
            return 1;
        case NodeKind::OUTPUT:
        case NodeKind::STEREO:
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

void MergeProcessor::mix_into(const float* a, const float* b, float* out, size_t n) const {
    const float m = std::clamp(mix.load(std::memory_order_relaxed), 0.0f, 1.0f);
    for (size_t i = 0; i < n; ++i) {
        out[i] = (1.0f - m) * a[i] + m * b[i];
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

} // namespace

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

    // Reachable from the Input (in processing order, a node's sources come first).
    vector<bool> live(nodes.size(), false);
    for (const size_t i : order) {
        if (nodes[i]->kind == NodeKind::INPUT) {
            live[i] = true;
            continue;
        }
        for (const auto& w : wires) {
            if (w.to_node == nodes[i]->id && live[index.at(w.from_node)]) {
                live[i] = true;
            }
        }
    }

    auto plan = make_unique<GraphPlan>();
    plan->nodes = nodes;
    plan->buffers.emplace_back(); // SILENCE
    const int scratch = 1;
    plan->buffers.emplace_back();
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
            step.out[1] = scratch; // EffectBase::process writes its copy of L there
        }
        out_buffers[node->id] = outs;

        if (node->kind == NodeKind::INPUT) {
            plan->input_buffer = outs[0];
            plan->input = static_cast<IoProcessor*>(&node->processor());
        } else if (node->kind == NodeKind::OUTPUT) {
            plan->output = static_cast<IoProcessor*>(&node->processor());
            // One side wired = mono out on both.
            plan->out_buffer[0] = step.in_wired[0] ? step.in[0] : step.in[1];
            plan->out_buffer[1] = step.in_wired[1] ? step.in[1] : step.in[0];
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
    plan->reserve(capacity);
    return plan;
}

void run_plan(GraphPlan& plan, size_t n) {
    auto buf = [&plan](int i) {
        return plan.buffers[static_cast<size_t>(i)].data();
    };
    for (auto& step : plan.steps) {
        EffectBase& fx = step.node->processor();
        fx.consume_pending_reset();
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
                const bool both = step.in_wired[0] && step.in_wired[1];
                // One side wired: that side is the mono source of the effect's stereo out.
                const int mono_in = step.in_wired[0] ? step.in[0] : step.in[1];
                std::copy_n(buf(both ? step.in[0] : mono_in), n, l);
                std::copy_n(buf(both ? step.in[1] : mono_in), n, r);
                fx.process(l, r, n, both);
                break;
            }
            case NodeKind::MERGE: {
                float* out = buf(step.out[0]);
                if (fx.is_enabled()) {
                    static_cast<MergeProcessor&>(fx).mix_into(buf(step.in[0]), buf(step.in[1]), out,
                                                              n);
                } else {
                    std::copy_n(buf(step.in[0]), n, out);
                }
                break;
            }
        }
    }
}

} // namespace namx
