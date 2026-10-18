#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "Common.h"
#include "EffectBase.h"

namespace namx
{

// The signal graph (ui/node-graph): nodes with mono ports, wired freely.
//
// Every port carries one mono signal. An output port may feed any number of
// inputs (that is how the signal splits — there is no split node); an input
// port takes at most one wire, so two signals meet only in a Merge node.
//
//   kind     inputs        outputs       processing
//   -------  ------------  ------------  ----------------------------------------
//   INPUT    —             1 (guitar)    the capture port after the input gain
//   OUTPUT   L, R          —             R unwired = L (and the other way round)
//   MONO     1             1             the effect's mono path
//   STEREO   L, R          L, R          R unwired = mono in → the effect's stereo
//                                         out; both wired = its stereo-in path
//   MERGE    A, B          1             (1 − mix)·A + mix·B
//
// A node's processor is an EffectBase (Input/Output/Merge have small ones of
// their own), so enable/bypass, parameters and resets work the same everywhere.

enum class NodeKind
{
    INPUT,
    OUTPUT,
    MONO,
    STEREO,
    MERGE
};

int input_count(NodeKind kind);
int output_count(NodeKind kind);

// Input and Output nodes: a smoothed gain; the Output also carries the limiter switch.
class IoProcessor : public EffectBase
{
public:
    IoProcessor() {
        set_enabled(true);
    }
    void reset() override {
    }
    void set_gain_db(float db) {
        gain_db.store(db, std::memory_order_relaxed);
    }
    float get_gain_db() const {
        return gain_db.load(std::memory_order_relaxed);
    }
    void set_limiter(bool on) {
        limiter.store(on, std::memory_order_relaxed);
    }
    bool get_limiter() const {
        return limiter.load(std::memory_order_relaxed);
    }
    // Audio thread: the gain for this block's ramp.
    float smoothed = 1.0f;

private:
    atomic<float> gain_db{0.0f};
    atomic<bool> limiter{false};
};

// Two mono inputs → one: a crossfade (bypassed = A only).
class MergeProcessor : public EffectBase
{
public:
    MergeProcessor() {
        set_enabled(true);
    }
    void reset() override {
    }
    void set_mix_pct(float pct) {
        mix.store(pct / 100.0f, std::memory_order_relaxed);
    }
    // Audio thread. `out` may not alias `a` or `b`.
    void mix_into(const float* a, const float* b, float* out, size_t n) const;

private:
    atomic<float> mix{0.5f};
};

// One node: its kind, catalog type and processor. Shared by the GUI's model
// and every plan that runs it, so a node removed from the graph lives on until
// the audio thread has provably left the last plan that held it.
class GraphNode
{
public:
    GraphNode(int new_id, string new_type, NodeKind new_kind, unique_ptr<EffectBase> new_fx)
        : id(new_id)
        , type(std::move(new_type))
        , kind(new_kind)
        , fx(std::move(new_fx)) {
    }
    const int id;
    const string type; // NodeCatalog type ID ("nam", "delay", "input", …)
    const NodeKind kind;
    EffectBase& processor() const {
        return *fx;
    }

private:
    unique_ptr<EffectBase> fx;
};

struct GraphWire
{
    int from_node = 0;
    int from_port = 0;
    int to_node = 0;
    int to_port = 0;
    bool operator==(const GraphWire&) const = default;
};

// A graph compiled for the audio thread: nodes in processing order, each port
// mapped to a buffer. Built and freed on the GUI thread; run on the audio thread.
class GraphPlan
{
public:
    // Buffers that never change: silence for unwired inputs.
    static constexpr int SILENCE = 0;

    struct Step
    {
        GraphNode* node;
        int in[2]; // buffer per input port
        int out[2]; // buffer per output port (−1 = none)
        bool in_wired[2];
    };

    vector<Step> steps;
    vector<vector<float>> buffers;
    vector<std::shared_ptr<GraphNode>> nodes; // keeps every node of the graph alive
    int input_buffer = -1; // the Input node's output (−1: no Input node)
    IoProcessor* input = nullptr;
    IoProcessor* output = nullptr;
    int out_buffer[2] = {SILENCE, SILENCE};
    size_t capacity = 0; // frames per buffer

    // Grows every buffer (not while the audio thread can run this plan).
    void reserve(size_t frames);
};

// Compiles `wires` over `nodes`: rejects a wire to an unknown node or port, a second
// wire into one input, and cycles. Only nodes reachable from the Input run; the rest
// stay idle (their outputs read as silence).
expected<unique_ptr<GraphPlan>, string>
compile_graph(const vector<std::shared_ptr<GraphNode>>& nodes, const vector<GraphWire>& wires,
              size_t capacity);

// `wires` with `wire` connected: a wire already into the same input is replaced (an
// input takes one wire), a duplicate is not added twice.
vector<GraphWire> with_wire(const vector<GraphWire>& wires, const GraphWire& wire);

// True when connecting `wire` (replacing whatever feeds that input — with_wire) leaves
// a valid graph: ports that exist, no loop. A node can't feed itself.
bool can_connect(const vector<std::shared_ptr<GraphNode>>& nodes, const vector<GraphWire>& wires,
                 const GraphWire& wire);

// True when adding `wire` keeps the graph acyclic and the target input free.
bool can_add_wire(const vector<std::shared_ptr<GraphNode>>& nodes, const vector<GraphWire>& wires,
                  const GraphWire& wire);

// Audio thread: runs every step; the Input's buffer must already hold the guitar.
void run_plan(GraphPlan& plan, size_t n);

} // namespace namx
