#include "GraphLayout.h"

#include <algorithm>
#include <numeric>
#include <optional>

namespace namx::GraphLayout
{

namespace
{

constexpr double NODE_GAP = 50.0; // between neighbouring cards
constexpr double LANE_W = 24.0; // a wire's lane through a row
constexpr double PARK_GAP = 120.0; // from the graph to the column of unwired nodes
constexpr int SWEEPS = 4;

struct Item
{
    int node = -1; // index into the nodes, −1 = a lane
    int layer = 0;
    double width = NODE_W;
    double x = 0.0; // centre
    double order = 0.0; // sort key within the row
};

// An edge between items in neighbouring rows, with its ports' offsets from the
// items' centres.
struct Edge
{
    int from;
    int to;
    double from_off;
    double to_off;
};

// Places `row` (item indices in order) as close to `desired` (centre x per item) as
// the minimum spacing allows, keeping the order.
void place_row(vector<Item>& items, const vector<int>& row, const vector<double>& desired) {
    if (row.empty()) {
        return;
    }
    vector<double> x(row.size());
    for (size_t i = 0; i < row.size(); ++i) {
        x[i] = desired[i];
        if (i > 0) {
            const Item& prev = items[static_cast<size_t>(row[i - 1])];
            const Item& cur = items[static_cast<size_t>(row[i])];
            const double gap = prev.node < 0 || cur.node < 0 ? NODE_GAP / 2 : NODE_GAP;
            x[i] = std::max(x[i], x[i - 1] + (prev.width + cur.width) / 2 + gap);
        }
    }
    // Pushing apart only moves right: shift the row back by the mean drift.
    double drift = 0.0;
    for (size_t i = 0; i < row.size(); ++i) {
        drift += x[i] - desired[i];
    }
    drift /= static_cast<double>(row.size());
    for (size_t i = 0; i < row.size(); ++i) {
        items[static_cast<size_t>(row[i])].x = x[i] - drift;
    }
}

} // namespace

Result compute(const vector<NodeInfo>& nodes, const vector<GraphWire>& wires) {
    Result result;
    result.routes.resize(wires.size());
    std::unordered_map<int, size_t> index;
    for (size_t i = 0; i < nodes.size(); ++i) {
        index[nodes[i].id] = i;
    }
    auto valid = [&index](const GraphWire& w) {
        return index.contains(w.from_node) && index.contains(w.to_node);
    };

    // 1. Rows by longest path (the graph is a DAG: the model only holds those).
    vector<bool> wired(nodes.size(), false);
    vector<int> pending(nodes.size(), 0);
    for (const auto& w : wires) {
        if (valid(w)) {
            wired[index[w.from_node]] = wired[index[w.to_node]] = true;
            ++pending[index[w.to_node]];
        }
    }
    vector<int> layer(nodes.size(), 0);
    vector<size_t> queue;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (pending[i] == 0) {
            queue.push_back(i);
        }
    }
    for (size_t k = 0; k < queue.size(); ++k) {
        const size_t u = queue[k];
        for (const auto& w : wires) {
            if (valid(w) && index[w.from_node] == u) {
                const size_t v = index[w.to_node];
                layer[v] = std::max(layer[v], layer[u] + 1);
                if (--pending[v] == 0) {
                    queue.push_back(v);
                }
            }
        }
    }
    auto placed = [&nodes, &wired](size_t i) {
        return wired[i] || nodes[i].kind == NodeKind::INPUT || nodes[i].kind == NodeKind::OUTPUT;
    };
    int deepest = 0;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (placed(i) && nodes[i].kind != NodeKind::OUTPUT) {
            deepest = std::max(deepest, layer[i]);
        }
    }
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].kind == NodeKind::INPUT) {
            layer[i] = 0;
        } else if (nodes[i].kind == NodeKind::OUTPUT) {
            layer[i] = std::max(layer[i], deepest + 1); // alone at the bottom
        }
    }

    // 2. Items: placed nodes, plus a lane per row a wire skips.
    vector<Item> items;
    vector<int> item_of(nodes.size(), -1);
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (placed(i)) {
            item_of[i] = static_cast<int>(items.size());
            items.push_back({static_cast<int>(i), layer[i], NODE_W, 0.0, 0.0});
        }
    }
    vector<Edge> edges;
    vector<vector<int>> lanes(wires.size()); // lane items per wire, top to bottom
    for (size_t wi = 0; wi < wires.size(); ++wi) {
        const GraphWire& w = wires[wi];
        if (!valid(w)) {
            continue;
        }
        const size_t u = index[w.from_node];
        const size_t v = index[w.to_node];
        const double from_off = port_x(output_count(nodes[u].kind), w.from_port) - NODE_W / 2;
        const double to_off = port_x(input_count(nodes[v].kind), w.to_port) - NODE_W / 2;
        int prev = item_of[u];
        double prev_off = from_off;
        for (int l = layer[u] + 1; l < layer[v]; ++l) {
            const int lane = static_cast<int>(items.size());
            items.push_back({-1, l, LANE_W, 0.0, 0.0});
            lanes[wi].push_back(lane);
            edges.push_back({prev, lane, prev_off, 0.0});
            prev = lane;
            prev_off = 0.0;
        }
        edges.push_back({prev, item_of[v], prev_off, to_off});
    }

    // 3. Order within rows.
    int rows = 0;
    for (const Item& it : items) {
        rows = std::max(rows, it.layer + 1);
    }
    vector<vector<int>> row(static_cast<size_t>(rows));
    for (size_t i = 0; i < items.size(); ++i) {
        row[static_cast<size_t>(items[i].layer)].push_back(static_cast<int>(i));
    }
    auto renumber = [&items](const vector<int>& r) {
        for (size_t k = 0; k < r.size(); ++k) {
            items[static_cast<size_t>(r[k])].order = static_cast<double>(k);
        }
    };
    for (const auto& r : row) {
        renumber(r);
    }
    // Barycenter of an item's neighbours above (down = true) or below.
    auto barycenter = [&items, &edges](int item, bool down) -> std::optional<double> {
        double sum = 0.0;
        int count = 0;
        for (const Edge& e : edges) {
            if (down && e.to == item) {
                sum += items[static_cast<size_t>(e.from)].order + e.from_off / NODE_W;
                ++count;
            } else if (!down && e.from == item) {
                sum += items[static_cast<size_t>(e.to)].order + e.to_off / NODE_W;
                ++count;
            }
        }
        if (count == 0) {
            return std::nullopt;
        }
        return sum / count;
    };
    auto sweep = [&](size_t r, bool down) {
        vector<std::pair<double, int>> keyed;
        for (const int it : row[r]) {
            keyed.push_back(
                {barycenter(it, down).value_or(items[static_cast<size_t>(it)].order), it});
        }
        std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
        for (size_t k = 0; k < keyed.size(); ++k) {
            row[r][k] = keyed[k].second;
        }
        renumber(row[r]);
    };
    for (int s = 0; s < SWEEPS; ++s) {
        for (size_t r = 1; r < row.size(); ++r) {
            sweep(r, true);
        }
        for (size_t r = row.size(); r-- > 1;) {
            sweep(r - 1, false);
        }
    }

    // 4. x: start packed, then align with the neighbours above / below.
    for (const auto& r : row) {
        vector<double> desired(r.size(), 0.0);
        place_row(items, r, desired);
    }
    auto align = [&](size_t r, bool down) {
        vector<double> desired;
        for (const int it : row[r]) {
            double sum = 0.0;
            int count = 0;
            for (const Edge& e : edges) {
                if (down && e.to == it) {
                    sum += items[static_cast<size_t>(e.from)].x + e.from_off - e.to_off;
                    ++count;
                } else if (!down && e.from == it) {
                    sum += items[static_cast<size_t>(e.to)].x + e.to_off - e.from_off;
                    ++count;
                }
            }
            desired.push_back(count ? sum / count : items[static_cast<size_t>(it)].x);
        }
        place_row(items, row[r], desired);
    };
    for (int s = 0; s < SWEEPS; ++s) {
        for (size_t r = row.size(); r-- > 1;) {
            align(r - 1, false);
        }
        for (size_t r = 1; r < row.size(); ++r) {
            align(r, true);
        }
    }

    // The Input at x = 0.
    double origin = 0.0;
    for (const Item& it : items) {
        if (it.node >= 0 && nodes[static_cast<size_t>(it.node)].kind == NodeKind::INPUT) {
            origin = it.x;
        }
    }
    auto row_y = [](int l) {
        return l * (NODE_H + ROW_GAP);
    };
    double right = 0.0;
    for (Item& it : items) {
        it.x -= origin;
        right = std::max(right, it.x + it.width / 2);
        if (it.node >= 0) {
            result.positions[nodes[static_cast<size_t>(it.node)].id] =
                QPointF(it.x - NODE_W / 2, row_y(it.layer));
        }
    }
    for (size_t wi = 0; wi < wires.size(); ++wi) {
        for (const int lane : lanes[wi]) {
            const Item& it = items[static_cast<size_t>(lane)];
            result.routes[wi].push_back(QPointF(it.x, row_y(it.layer)));
            result.routes[wi].push_back(QPointF(it.x, row_y(it.layer) + NODE_H));
        }
    }

    // Unwired nodes: a column to the right, in creation order.
    double park_y = 0.0;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (!placed(i)) {
            result.positions[nodes[i].id] = QPointF(right + PARK_GAP, park_y);
            park_y += NODE_H + ROW_GAP;
        }
    }
    return result;
}

} // namespace namx::GraphLayout
