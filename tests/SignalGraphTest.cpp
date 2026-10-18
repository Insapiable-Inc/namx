// Headless test of the signal graph (ui/node-graph, src/dsp/SignalGraph.h): the
// wiring rules — one wire per input, no loops — and what a compiled plan does with
// a block: fan-out from one output, a Merge's mix, a bypassed node passing through,
// an Output with one side wired playing it on both, a node nothing feeds staying
// silent, two branches of different latency lined up where they meet. Fixed ramps as
// input; nothing touches the disk.

#include "SignalGraph.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "NodeCatalog.h"

namespace namx
{

static int failures = 0;
#define CHECK(cond, msg) \
    do { \
        if (cond) \
            std::printf("  ok   %s\n", msg); \
        else { \
            std::printf("  FAIL %s  (%s:%d)\n", msg, __FILE__, __LINE__); \
            ++failures; \
        } \
    } while (0)

using Nodes = vector<std::shared_ptr<GraphNode>>;
constexpr size_t N = 64;

// Runs one block of 0, 1/N, 2/N, … through the graph; the output's L and R.
static std::pair<vector<float>, vector<float>> run(const Nodes& nodes,
                                                   const vector<GraphWire>& wires) {
    auto plan = compile_graph(nodes, wires, N);
    if (!plan) {
        return {};
    }
    GraphPlan& p = **plan;
    float* in = p.buffers[static_cast<size_t>(p.input_buffer)].data();
    for (size_t i = 0; i < N; ++i) {
        in[i] = static_cast<float>(i) / N;
    }
    run_plan(p, N);
    const auto& l = p.buffers[static_cast<size_t>(p.out_buffer[0])];
    const auto& r = p.buffers[static_cast<size_t>(p.out_buffer[1])];
    return {vector<float>(l.begin(), l.begin() + N), vector<float>(r.begin(), r.begin() + N)};
}

static bool equals_scaled(const vector<float>& v, float scale) {
    if (v.size() != N) {
        return false;
    }
    for (size_t i = 0; i < N; ++i) {
        if (std::abs(v[i] - scale * static_cast<float>(i) / N) > 1e-6f) {
            return false;
        }
    }
    return true;
}

static void test_rules() {
    std::printf("wiring rules\n");
    const Nodes nodes = {NodeCatalog::create(1, "input", false),
                         NodeCatalog::create(2, "drive", false),
                         NodeCatalog::create(3, "output", false)};
    const vector<GraphWire> chain = {{1, 0, 2, 0}, {2, 0, 3, 0}};
    CHECK(compile_graph(nodes, chain, N).has_value(), "a chain compiles");
    CHECK(!can_add_wire(nodes, chain, {1, 0, 3, 0}), "a second wire into a taken input is refused");
    CHECK(can_add_wire(nodes, chain, {2, 0, 3, 1}), "an output feeds a second input");
    CHECK(!can_add_wire(nodes, chain, {2, 0, 2, 0}), "a node can't feed itself");
    const Nodes loop_nodes = {
        NodeCatalog::create(1, "input", false), NodeCatalog::create(2, "drive", false),
        NodeCatalog::create(3, "eq", false), NodeCatalog::create(4, "output", false)};
    const vector<GraphWire> loop_chain = {{1, 0, 2, 0}, {2, 0, 3, 0}, {3, 0, 4, 0}};
    CHECK(!can_add_wire(loop_nodes, {{2, 0, 3, 0}}, {3, 0, 2, 0}), "a loop is refused");
    CHECK(!compile_graph(loop_nodes, {{2, 0, 3, 0}, {3, 0, 2, 0}}, N).has_value(),
          "a graph with a loop doesn't compile");
    CHECK(!can_add_wire(loop_nodes, loop_chain, {3, 1, 4, 1}), "a port a node lacks is refused");
    CHECK(!NodeCatalog::create(9, "nam", true), "no stereo Amp variant: Amp and Cab are mono");
    {
        // A stereo node takes one input: mono in, L and R out.
        const Nodes nodes = {NodeCatalog::create(1, "input", false),
                             NodeCatalog::create(2, "tremolo", true),
                             NodeCatalog::create(3, "output", false)};
        const vector<GraphWire> wires = {{1, 0, 2, 0}, {2, 0, 3, 0}, {2, 1, 3, 1}};
        CHECK(compile_graph(nodes, wires, N).has_value(), "mono in, stereo out compiles");
        CHECK(!can_connect(nodes, wires, {1, 0, 2, 1}), "a stereo node has no second input");
    }

    // Connecting into a taken input replaces its wire; never into a loop.
    const vector<GraphWire> replaced = with_wire(loop_chain, {1, 0, 3, 0});
    CHECK(replaced.size() == loop_chain.size() &&
              std::find(replaced.begin(), replaced.end(), GraphWire{2, 0, 3, 0}) == replaced.end(),
          "connecting into a taken input replaces the old wire");
    CHECK(can_connect(loop_nodes, loop_chain, {1, 0, 3, 0}), "a replacing connect is allowed");
    CHECK(!can_connect(loop_nodes, loop_chain, {3, 0, 2, 0}),
          "a replacing connect that closes a loop is refused");
    CHECK(!can_connect(loop_nodes, loop_chain, {2, 0, 2, 0}), "a node can't feed itself");
    CHECK(!can_connect(loop_nodes, loop_chain, {3, 0, 1, 0}), "nothing feeds the Input");
}

// A mono effect that only delays, by DELAY samples, and reports it.
class LateEffect : public EffectBase
{
public:
    static constexpr size_t DELAY = 10;
    LateEffect() {
        set_enabled(true);
    }
    void reset() override {
        std::fill(std::begin(held), std::end(held), 0.0f);
    }
    double latency_ms() const override {
        return DELAY / get_sample_rate() * 1000.0;
    }

protected:
    void process_block_mono(float* buf, size_t n) override {
        for (size_t i = 0; i < n; ++i) {
            const float x = buf[i];
            buf[i] = held[i % DELAY];
            held[i % DELAY] = x;
        }
    }

private:
    float held[DELAY] = {};
};

static void test_processing() {
    std::printf("processing\n");
    // Input fans out to both sides of a Merge: at 50 % the input comes back.
    {
        auto merge = NodeCatalog::create(2, "merge", false);
        const Nodes nodes = {NodeCatalog::create(1, "input", false), merge,
                             NodeCatalog::create(3, "output", false)};
        const auto [l, r] = run(nodes, {{1, 0, 2, 0}, {1, 0, 2, 1}, {2, 0, 3, 0}});
        CHECK(equals_scaled(l, 1.0f), "fan-out into a 50 % merge = the input");
        CHECK(equals_scaled(r, 1.0f), "one side wired into the Output = both sides");
        static_cast<MergeProcessor&>(merge->processor()).set_mix_pct(100.0f);
        merge->processor().reset(); // past the glide
        const Nodes half = {NodeCatalog::create(1, "input", false), merge,
                            NodeCatalog::create(3, "output", false)};
        const auto [l2, r2] = run(half, {{1, 0, 2, 0}, {2, 0, 3, 0}});
        CHECK(equals_scaled(l2, 0.0f), "mix 100 % with B unwired = silence");
    }
    // A bypassed effect passes the signal through untouched.
    {
        auto drive = NodeCatalog::create(2, "drive", false);
        drive->processor().set_enabled(false);
        const Nodes nodes = {NodeCatalog::create(1, "input", false), drive,
                             NodeCatalog::create(3, "output", false)};
        const auto [l, r] = run(nodes, {{1, 0, 2, 0}, {2, 0, 3, 0}});
        CHECK(equals_scaled(l, 1.0f), "a bypassed node passes through");
    }
    // An Output fed only by a node nothing feeds stays silent.
    {
        const Nodes nodes = {NodeCatalog::create(1, "input", false),
                             NodeCatalog::create(2, "drive", false),
                             NodeCatalog::create(3, "output", false)};
        const auto [l, r] = run(nodes, {{2, 0, 3, 0}});
        CHECK(equals_scaled(l, 0.0f) && equals_scaled(r, 0.0f), "an unfed branch is silent");
    }
    // A branch with latency into a Merge: the direct branch waits for it, so A and B
    // line up — and Invert B at 50 % cancels them exactly.
    {
        auto merge = NodeCatalog::create(3, "merge", false);
        auto late =
            std::make_shared<GraphNode>(2, "late", NodeKind::MONO, std::make_unique<LateEffect>());
        const Nodes nodes = {NodeCatalog::create(1, "input", false), late, merge,
                             NodeCatalog::create(4, "output", false)};
        const vector<GraphWire> wires = {{1, 0, 2, 0}, {2, 0, 3, 0}, {1, 0, 3, 1}, {3, 0, 4, 0}};
        const auto [l, r] = run(nodes, wires);
        bool aligned = l.size() == N;
        for (size_t i = 0; aligned && i < N; ++i) {
            const float want =
                i < LateEffect::DELAY ? 0.0f : static_cast<float>(i - LateEffect::DELAY) / N;
            aligned = std::abs(l[i] - want) < 1e-6f;
        }
        CHECK(aligned, "a direct branch waits for a late one at a Merge");
        static_cast<MergeProcessor&>(merge->processor()).set_invert_b(true);
        late->processor().reset(); // a fresh plan: fresh delay lines
        merge->processor().reset(); // past the glide
        const auto [l2, r2] = run(nodes, wires);
        CHECK(equals_scaled(l2, 0.0f), "Invert B at 50 % cancels two aligned copies");
    }
    // A dead end (fed, but reaching no Output) doesn't run.
    {
        const Nodes nodes = {NodeCatalog::create(1, "input", false),
                             NodeCatalog::create(2, "drive", false),
                             NodeCatalog::create(3, "output", false)};
        const vector<GraphWire> wires = {{1, 0, 2, 0}, {1, 0, 3, 0}};
        const auto plan = compile_graph(nodes, wires, N);
        const bool drive_runs = plan && std::any_of((*plan)->steps.begin(), (*plan)->steps.end(),
                                                    [](const GraphPlan::Step& st) {
                                                        return st.node->id == 2;
                                                    });
        CHECK(plan && !drive_runs, "a dead end isn't processed");
        CHECK(live_node_ids(nodes, wires) == vector<int>({1, 3}), "live nodes: Input to Output");
    }
}

} // namespace namx

int main() {
    namx::test_rules();
    namx::test_processing();
    std::printf(namx::failures ? "\n%d FAILED\n" : "\nall passed\n", namx::failures);
    return namx::failures ? 1 : 0;
}
