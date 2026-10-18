#pragma once

#include <unordered_map>
#include <vector>

#include <QPointF>

#include "Common.h"
#include "SignalGraph.h"

namespace namx
{

// North-to-south layout of the node graph (ui/node-graph), applied by Auto-Organize
// (and to files saved without positions): places follow from the wiring. Every card
// is the same size.
namespace GraphLayout
{

constexpr double NODE_W = 230.0;
constexpr double NODE_H = 72.0;
constexpr double ROW_GAP = 58.0; // between a row's bottom ports and the next row's top ports

// Horizontal position of port `i` of `count` along a card's edge.
inline double port_x(int count, int i) {
    return NODE_W * (i + 1) / (count + 1);
}

struct NodeInfo
{
    int id;
    NodeKind kind;
};

struct Result
{
    std::unordered_map<int, QPointF> positions; // node ID → card's top-left corner
    // Per wire (same order as the input): where it bends on its way past the rows
    // between its ends — a top and a bottom point per row crossed, in order. Empty for
    // a wire between neighbouring rows.
    vector<vector<QPointF>> routes;
};

// Layered layout (Sugiyama style):
// 1. rows: each node one row below its deepest source (longest path); the Input on the
//    first row, the Output alone on the last; a node with no wires at all waits in a
//    column to the right;
// 2. a wire that skips rows gets a lane (a placeholder) in each row it crosses, so it
//    never runs through a card;
// 3. order within rows: barycenter sweeps down and up (fewer crossings), ties by
//    creation order;
// 4. x: each card centred under the ports that feed it (then over the ones it feeds),
//    pushed apart where they would overlap; the Input at x = 0.
Result compute(const vector<NodeInfo>& nodes, const vector<GraphWire>& wires);

} // namespace GraphLayout

} // namespace namx
