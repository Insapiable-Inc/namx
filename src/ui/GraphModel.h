#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointF>
#include <QString>

#include "Common.h"
#include "NodeCatalog.h"
#include "SignalGraph.h"

namespace namx
{

class AudioEngine;

// The rig as the user edits it (ui/node-graph): nodes with a name, a place on the
// canvas and their parameter values, and the wires between them. The single source
// of truth for presets and the session — the canvas and the parameter panel read
// it and change it only through these calls; every change that alters the sound
// is pushed to the engine (AudioEngine::set_graph) at once. Nodes sit where the user
// puts them; auto_organize() lays the graph out once, north to south (GraphLayout).
class GraphModel : public QObject
{
    Q_OBJECT

public:
    struct Node
    {
        std::shared_ptr<GraphNode> dsp;
        const NodeType* type = nullptr;
        QString name;
        QPointF pos; // card's top-left corner on the canvas
        bool enabled = true;
        QHash<QString, double> values; // every parameter of the type
        QString file; // model / IR path; empty = none

        int id() const {
            return dsp->id;
        }
        NodeKind kind() const {
            return dsp->kind;
        }
        bool stereo() const {
            return dsp->kind == NodeKind::STEREO;
        }
    };

    explicit GraphModel(AudioEngine& new_engine, QObject* parent = nullptr);

    const vector<Node>& get_nodes() const {
        return nodes;
    }
    const vector<GraphWire>& get_wires() const {
        return wires;
    }
    // Where wire `i` (get_wires order) bends past the rows it skips (GraphLayout::Result):
    // set by auto_organize(), empty (a plain curve) once anything moves or the structure
    // changes.
    const vector<QPointF>& route(size_t i) const;
    const Node* node(int id) const;
    // On a path from the Input to the Output: the engine runs it (SignalGraph's
    // live_node_ids). Dead ends and unfed branches don't.
    bool is_live(int id) const {
        return std::find(live_ids.begin(), live_ids.end(), id) != live_ids.end();
    }
    int input_id() const;
    int output_id() const;

    // Structure. Each returns false (and changes nothing) when not allowed.
    // A new node: id, or 0 for an unknown type / variant. Its card's top-left at `at`,
    // else right of the others.
    int add_node(const QString& type_id, bool stereo, std::optional<QPointF> at = std::nullopt);
    // Input and Output can't be removed. The node's first input source is wired on to
    // whatever its first output fed, so removing a node from a chain keeps it joined.
    bool remove_node(int id);
    bool add_wire(const GraphWire& wire);
    // The user's connect: like add_wire, but a wire already into that input is
    // replaced. Refused (nothing changes) when the result would have a loop.
    bool connect(const GraphWire& wire);
    bool remove_wire(const GraphWire& wire);
    // `wire` becomes source → node (port 0) → target (port 0).
    bool insert_on_wire(int node_id, const GraphWire& wire);
    // Same type, other variant (mono ↔ stereo), values, file and position kept; wires
    // to ports the new variant lacks are dropped.
    bool set_stereo(int id, bool stereo);
    int duplicate_node(int id); // placed a little below and right of the original
    bool can_add_wire(const GraphWire& wire) const;
    bool can_connect(const GraphWire& wire) const;

    // Node state.
    void set_value(int id, const QString& key, double value);
    // Every parameter back to the catalog default (name, power and file kept).
    void reset_values(int id);
    void set_enabled(int id, bool on);
    void set_name(int id, const QString& name);
    // The user dragged the card.
    void set_position(int id, QPointF pos);
    // Every card placed by GraphLayout, once: rows by the wiring, north to south.
    void auto_organize();
    // Loads a model (Amp) or IR (Cab) into the node; on failure the error is returned
    // and the node keeps its old file.
    expected<void, QString> load_file(int id, const QString& path);

    // The global tempo: synced rates and times follow it (ParamSpec::sync_switch).
    void set_bpm(double new_bpm);
    double get_bpm() const {
        return bpm;
    }
    // What the effect gets for `p`: the knob's value, or its note value at the tempo while
    // synced.
    double effective_value(const Node& n, const ParamSpec& p) const;

    // App-wide model setting, applied to every Amp node (Settings → Audio).
    void set_normalize_models(bool on);

    // Whole graph.
    // The starting rig: Input → Amp → Cab → Output.
    void reset_to_default();
    QJsonObject to_json() const;
    // A v3 graph, or a v2 preset / session (a chain: enabled effects in effectOrder,
    // mono effects doubled after a stereo one, as the chain ran them). Files that
    // can't be loaded are reported in `warnings`, the rest still loads. Returns false
    // (graph unchanged) when the object is neither.
    bool from_json(const QJsonObject& obj, QStringList* warnings = nullptr);

    // MIDI target of a node parameter: "param:<node name>/<key>" ("enabled" = the power
    // switch). By name, so a mapping follows "Delay" from preset to preset.
    static QString midi_target_id(const QString& node_name, const QString& key) {
        return "param:" + node_name + "/" + key;
    }

    // A unique display name for a new node of `type` ("Amp", "Amp 2", …).
    QString unique_name(const QString& base) const;

signals:
    // Nodes or wires added or removed: the canvas rebuilds.
    void structure_changed();
    // One node's name, state, values, file or wiring summary changed.
    void node_changed(int id);
    // Something a preset stores changed (positions included): session save, `*`.
    void modified();

private:
    Node* find(int id);
    void push(); // publish the graph to the engine (routes dropped)
    void lay_out(); // GraphLayout: positions and routes
    bool is_organized() const; // every card where lay_out() would put it
    void clear();
    int create(const QString& type_id, bool stereo, int id = 0);
    void apply_all(Node& n);
    void apply_param(Node& n, const ParamSpec& p);
    QString legacy_file_error(const QString& what, const QString& path, const QString& error) const;
    bool from_v2(const QJsonObject& obj, QStringList* warnings);
    bool from_v3(const QJsonObject& graph, QStringList* warnings);

    AudioEngine& engine;
    vector<Node> nodes;
    vector<GraphWire> wires;
    vector<vector<QPointF>> routes; // parallel to wires
    vector<int> live_ids; // from push()
    int next_id = 1;
    bool normalize_models = true;
    double bpm = 120.0;
};

} // namespace namx
