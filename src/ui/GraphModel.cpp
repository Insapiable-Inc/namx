#include "GraphModel.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonValue>

#include "AudioEngine.h"
#include "GraphLayout.h"
#include "IRProcessor.h"
#include "Log.h"
#include "NamProcessor.h"
#include "TempoSync.h"

namespace namx
{

namespace
{

constexpr int FORMAT_VERSION = 3;
static_assert(TempoSync::NOTES.size() == 16, "NodeCatalog's note params index NOTES 0 … 15");

} // namespace

GraphModel::GraphModel(AudioEngine& new_engine, QObject* parent)
    : QObject(parent)
    , engine(new_engine) {
}

const GraphModel::Node* GraphModel::node(int id) const {
    for (const auto& n : nodes) {
        if (n.id() == id) {
            return &n;
        }
    }
    return nullptr;
}

GraphModel::Node* GraphModel::find(int id) {
    return const_cast<Node*>(std::as_const(*this).node(id));
}

int GraphModel::input_id() const {
    for (const auto& n : nodes) {
        if (n.kind() == NodeKind::INPUT) {
            return n.id();
        }
    }
    return 0;
}

int GraphModel::output_id() const {
    for (const auto& n : nodes) {
        if (n.kind() == NodeKind::OUTPUT) {
            return n.id();
        }
    }
    return 0;
}

QString GraphModel::unique_name(const QString& base) const {
    auto taken = [this](const QString& name) {
        return std::any_of(nodes.begin(), nodes.end(), [&name](const Node& n) {
            return n.name == name;
        });
    };
    if (!taken(base)) {
        return base;
    }
    for (int i = 2;; ++i) {
        const QString name = QString("%1 %2").arg(base).arg(i);
        if (!taken(name)) {
            return name;
        }
    }
}

const vector<QPointF>& GraphModel::route(size_t i) const {
    static const vector<QPointF> NONE;
    return i < routes.size() ? routes[i] : NONE;
}

void GraphModel::lay_out() {
    vector<GraphLayout::NodeInfo> info;
    info.reserve(nodes.size());
    for (const auto& n : nodes) {
        info.push_back({n.id(), n.kind()});
    }
    GraphLayout::Result layout = GraphLayout::compute(info, wires);
    for (auto& n : nodes) {
        n.pos = layout.positions[n.id()];
    }
    routes = std::move(layout.routes);
}

bool GraphModel::is_organized() const {
    vector<GraphLayout::NodeInfo> info;
    info.reserve(nodes.size());
    for (const auto& n : nodes) {
        info.push_back({n.id(), n.kind()});
    }
    const GraphLayout::Result layout = GraphLayout::compute(info, wires);
    return std::all_of(nodes.begin(), nodes.end(), [&layout](const Node& n) {
        const QPointF d = layout.positions.at(n.id()) - n.pos;
        return std::abs(d.x()) < 0.5 && std::abs(d.y()) < 0.5;
    });
}

void GraphModel::auto_organize() {
    lay_out();
    emit structure_changed();
    emit modified();
}

void GraphModel::set_position(int id, QPointF pos) {
    Node* n = find(id);
    if (!n || n->pos == pos) {
        return;
    }
    n->pos = pos;
    routes.clear(); // the lanes were for the old places
    emit modified();
}

void GraphModel::push() {
    routes.clear();
    vector<std::shared_ptr<GraphNode>> dsp;
    dsp.reserve(nodes.size());
    for (const auto& n : nodes) {
        dsp.push_back(n.dsp);
    }
    live_ids = live_node_ids(dsp, wires);
    if (auto ok = engine.set_graph(dsp, wires); !ok) {
        // The model only ever holds graphs can_add_wire accepted: a bug if reached.
        qCWarning(lc_audio, "[GraphModel] graph rejected: %s", ok.error().c_str());
    }
}

double GraphModel::effective_value(const Node& n, const ParamSpec& p) const {
    const double free = n.values.value(QString::fromStdString(p.key), p.def);
    if (p.sync_switch.empty() || n.values.value(QString::fromStdString(p.sync_switch)) <= 0.5) {
        return free;
    }
    const double note = n.values.value(QString::fromStdString(p.sync_note));
    const double v = p.sync_time ? TempoSync::note_ms(bpm, note) : TempoSync::note_hz(bpm, note);
    return std::clamp(v, p.min, p.sync_max > 0.0 ? p.sync_max : p.max);
}

void GraphModel::apply_param(Node& n, const ParamSpec& p) {
    if (p.apply) {
        p.apply(n.dsp->processor(), effective_value(n, p));
    }
}

void GraphModel::set_bpm(double new_bpm) {
    if (new_bpm <= 0.0 || new_bpm == bpm) {
        return;
    }
    bpm = new_bpm;
    for (auto& n : nodes) {
        for (const auto& p : n.type->params) {
            if (!p.sync_switch.empty()) {
                apply_param(n, p); // only synced ones change
            }
        }
    }
}

void GraphModel::apply_all(Node& n) {
    EffectBase& fx = n.dsp->processor();
    for (const auto& p : n.type->params) {
        apply_param(n, p);
    }
    fx.set_enabled(n.enabled);
    if (n.type->id == "nam") {
        static_cast<NamProcessor&>(fx).set_normalize(normalize_models);
    }
}

int GraphModel::create(const QString& type_id, bool stereo, int id) {
    const int new_id = id > 0 ? id : next_id;
    if (const NodeType* t = NodeCatalog::find(type_id.toStdString())) {
        stereo = t->variant(stereo); // an Amp asked for mono is the stereo Amp
    }
    std::shared_ptr<GraphNode> dsp = engine.create_node(new_id, type_id.toStdString(), stereo);
    if (!dsp) {
        return 0;
    }
    next_id = std::max(next_id, new_id + 1);
    Node n;
    n.dsp = std::move(dsp);
    n.type = NodeCatalog::find(type_id.toStdString());
    n.name = unique_name(QString::fromStdString(n.type->name));
    for (const auto& p : n.type->params) {
        n.values.insert(QString::fromStdString(p.key), p.def);
    }
    apply_all(n);
    nodes.push_back(std::move(n));
    return new_id;
}

int GraphModel::add_node(const QString& type_id, bool stereo, std::optional<QPointF> at) {
    if (type_id == "input" || type_id == "output") {
        return 0; // one of each, made with the graph
    }
    const int id = create(type_id, stereo);
    if (id) {
        if (!at) {
            // Right of the rig, level with its top.
            double right = 0.0;
            double top = 0.0;
            bool first = true;
            for (const auto& n : nodes) {
                if (n.id() == id) {
                    continue;
                }
                right = first ? n.pos.x() + GraphLayout::NODE_W
                              : std::max(right, n.pos.x() + GraphLayout::NODE_W);
                top = first ? n.pos.y() : std::min(top, n.pos.y());
                first = false;
            }
            at = QPointF(right + 60.0, top);
        }
        find(id)->pos = *at;
        push();
        emit structure_changed();
        emit modified();
    }
    return id;
}

bool GraphModel::remove_node(int id) {
    const Node* n = node(id);
    if (!n || n->kind() == NodeKind::INPUT || n->kind() == NodeKind::OUTPUT) {
        return false;
    }
    // Keep a chain joined: the source of input 0 takes over output 0's targets.
    std::optional<GraphWire> source;
    vector<GraphWire> targets;
    for (const auto& w : wires) {
        if (w.to_node == id && w.to_port == 0) {
            source = w;
        }
        if (w.from_node == id && w.from_port == 0) {
            targets.push_back(w);
        }
    }
    std::erase_if(wires, [id](const GraphWire& w) {
        return w.from_node == id || w.to_node == id;
    });
    std::erase_if(nodes, [id](const Node& x) {
        return x.id() == id;
    });
    if (source) {
        for (const auto& t : targets) {
            const GraphWire healed{source->from_node, source->from_port, t.to_node, t.to_port};
            if (can_add_wire(healed)) {
                wires.push_back(healed);
            }
        }
    }
    push();
    emit structure_changed();
    emit modified();
    return true;
}

bool GraphModel::can_add_wire(const GraphWire& wire) const {
    vector<std::shared_ptr<GraphNode>> dsp;
    dsp.reserve(nodes.size());
    for (const auto& n : nodes) {
        dsp.push_back(n.dsp);
    }
    return ::namx::can_add_wire(dsp, wires, wire);
}

bool GraphModel::can_connect(const GraphWire& wire) const {
    vector<std::shared_ptr<GraphNode>> dsp;
    dsp.reserve(nodes.size());
    for (const auto& n : nodes) {
        dsp.push_back(n.dsp);
    }
    return ::namx::can_connect(dsp, wires, wire);
}

bool GraphModel::connect(const GraphWire& wire) {
    if (!can_connect(wire)) {
        return false;
    }
    if (std::find(wires.begin(), wires.end(), wire) != wires.end()) {
        return true; // already connected just so
    }
    wires = with_wire(wires, wire);
    push();
    emit structure_changed();
    emit modified();
    return true;
}

bool GraphModel::add_wire(const GraphWire& wire) {
    if (!can_add_wire(wire)) {
        return false;
    }
    wires.push_back(wire);
    push();
    emit structure_changed();
    emit modified();
    return true;
}

bool GraphModel::remove_wire(const GraphWire& wire) {
    const auto it = std::find(wires.begin(), wires.end(), wire);
    if (it == wires.end()) {
        return false;
    }
    wires.erase(it);
    push();
    emit structure_changed();
    emit modified();
    return true;
}

bool GraphModel::insert_on_wire(int node_id, const GraphWire& wire) {
    const Node* n = node(node_id);
    if (!n || input_count(n->kind()) == 0 || output_count(n->kind()) == 0 ||
        std::find(wires.begin(), wires.end(), wire) == wires.end()) {
        return false;
    }
    const vector<GraphWire> before = wires;
    std::erase(wires, wire);
    const GraphWire in{wire.from_node, wire.from_port, node_id, 0};
    const GraphWire out{node_id, 0, wire.to_node, wire.to_port};
    if (!can_add_wire(in)) {
        wires = before;
        return false;
    }
    wires.push_back(in);
    if (!can_add_wire(out)) {
        wires = before;
        return false;
    }
    wires.push_back(out);
    push();
    emit structure_changed();
    emit modified();
    return true;
}

bool GraphModel::set_stereo(int id, bool stereo) {
    Node* old = find(id);
    if (!old || old->stereo() == stereo ||
        (old->kind() != NodeKind::MONO && old->kind() != NodeKind::STEREO) ||
        !(stereo ? old->type->has_stereo : old->type->has_mono)) {
        return false;
    }
    std::shared_ptr<GraphNode> dsp = engine.create_node(id, old->type->id, stereo);
    if (!dsp) {
        return false;
    }
    old->dsp = std::move(dsp);
    apply_all(*old);
    const NodeKind kind = old->kind();
    std::erase_if(wires, [id, kind](const GraphWire& w) {
        return (w.to_node == id && w.to_port >= input_count(kind)) ||
               (w.from_node == id && w.from_port >= output_count(kind));
    });
    push();
    emit structure_changed();
    emit modified();
    return true;
}

int GraphModel::duplicate_node(int id) {
    const Node* src = node(id);
    if (!src || src->kind() == NodeKind::INPUT || src->kind() == NodeKind::OUTPUT) {
        return 0;
    }
    const Node copy = *src; // `src` dangles once nodes grows
    const int new_id = create(QString::fromStdString(copy.type->id), copy.stereo());
    if (!new_id) {
        return 0;
    }
    Node* n = find(new_id);
    n->pos = copy.pos + QPointF(36.0, 36.0);
    n->values = copy.values;
    n->enabled = copy.enabled;
    apply_all(*n);
    if (!copy.file.isEmpty()) {
        (void)load_file(new_id, copy.file);
    }
    push();
    emit structure_changed();
    emit modified();
    return new_id;
}

void GraphModel::set_value(int id, const QString& key, double value) {
    Node* n = find(id);
    if (!n) {
        return;
    }
    for (const auto& p : n->type->params) {
        if (QString::fromStdString(p.key) == key) {
            value = std::clamp(value, p.min, p.max);
            if (n->values.value(key) == value) {
                return;
            }
            n->values[key] = value;
            // The parameter itself, and the ones whose Sync switch or note value it is.
            const string k = p.key;
            for (const auto& q : n->type->params) {
                if (q.key == k || q.sync_switch == k || q.sync_note == k) {
                    apply_param(*n, q);
                }
            }
            emit node_changed(id);
            emit modified();
            return;
        }
    }
}

void GraphModel::set_enabled(int id, bool on) {
    Node* n = find(id);
    if (!n || n->enabled == on || n->kind() == NodeKind::INPUT || n->kind() == NodeKind::OUTPUT) {
        return;
    }
    n->enabled = on;
    n->dsp->processor().set_enabled(on);
    emit node_changed(id);
    emit modified();
}

void GraphModel::set_name(int id, const QString& name) {
    Node* n = find(id);
    const QString trimmed = name.trimmed();
    if (!n || trimmed.isEmpty() || n->name == trimmed) {
        return;
    }
    if (std::any_of(nodes.begin(), nodes.end(), [&trimmed](const Node& x) {
            return x.name == trimmed;
        })) {
        return; // names are unique: MIDI targets go by name
    }
    n->name = trimmed;
    emit structure_changed(); // MIDI targets are re-registered under the new name
    emit node_changed(id);
    emit modified();
}

expected<void, QString> GraphModel::load_file(int id, const QString& path) {
    Node* n = find(id);
    if (!n || n->type->file == NodeType::File::NONE) {
        return std::unexpected(QString("This node takes no file."));
    }
    const string p = path.toStdString();
    bool ok = false;
    QString error;
    if (n->type->file == NodeType::File::MODEL) {
        ok = engine.load_model(*n->dsp, p);
        error = QString::fromStdString(
            static_cast<NamProcessor&>(n->dsp->processor()).get_last_error());
    } else {
        ok = engine.load_ir(*n->dsp, p);
        error = QString::fromStdString(static_cast<IRProcessor&>(n->dsp->processor()).last_error());
    }
    if (!ok) {
        return std::unexpected(error.isEmpty() ? QString("The file couldn't be loaded.") : error);
    }
    n->file = path;
    emit node_changed(id);
    emit modified();
    return {};
}

void GraphModel::set_normalize_models(bool on) {
    normalize_models = on;
    for (auto& n : nodes) {
        if (n.type->id == "nam") {
            static_cast<NamProcessor&>(n.dsp->processor()).set_normalize(on);
            emit node_changed(n.id());
        }
    }
}

void GraphModel::clear() {
    nodes.clear();
    wires.clear();
    next_id = 1;
}

void GraphModel::reset_to_default() {
    clear();
    const int in = create("input", false);
    const int amp = create("nam", false);
    const int cab = create("ir", false);
    const int out = create("output", false);
    wires = {{in, 0, amp, 0}, {amp, 0, cab, 0}, {cab, 0, out, 0}};
    push();
    lay_out();
    emit structure_changed();
    emit modified();
}

QJsonObject GraphModel::to_json() const {
    QJsonArray node_arr;
    for (const auto& n : nodes) {
        QJsonObject o;
        o["id"] = n.id();
        o["type"] = QString::fromStdString(n.type->id);
        if (n.stereo()) {
            o["stereo"] = true;
        }
        o["name"] = n.name;
        o["x"] = n.pos.x();
        o["y"] = n.pos.y();
        if (n.kind() != NodeKind::INPUT && n.kind() != NodeKind::OUTPUT) {
            o["enabled"] = n.enabled;
        }
        QJsonObject params;
        for (const auto& p : n.type->params) {
            const QString key = QString::fromStdString(p.key);
            params[key] = n.values.value(key, p.def);
        }
        o["params"] = params;
        if (!n.file.isEmpty()) {
            o["file"] = n.file;
        }
        node_arr.append(o);
    }
    QJsonArray wire_arr;
    for (const auto& w : wires) {
        wire_arr.append(QJsonObject{{"from", w.from_node},
                                    {"fromPort", w.from_port},
                                    {"to", w.to_node},
                                    {"toPort", w.to_port}});
    }
    QJsonObject obj;
    obj["version"] = FORMAT_VERSION;
    obj["graph"] = QJsonObject{{"nodes", node_arr}, {"wires", wire_arr}};
    return obj;
}

QString GraphModel::legacy_file_error(const QString& what, const QString& path,
                                      const QString& error) const {
    return QString("%1 \"%2\": %3").arg(what, QFileInfo(path).fileName(), error);
}

bool GraphModel::from_json(const QJsonObject& obj, QStringList* warnings) {
    if (obj["graph"].isObject()) {
        return from_v3(obj["graph"].toObject(), warnings);
    }
    if (obj.contains("effectOrder") || obj.contains("namPath") || obj.contains("eq") ||
        obj.contains("inputGain")) {
        return from_v2(obj, warnings);
    }
    return false;
}

bool GraphModel::from_v3(const QJsonObject& graph, QStringList* warnings) {
    const QJsonArray node_arr = graph["nodes"].toArray();
    // Validate before touching the live graph: an Input and an Output, or it isn't one.
    bool has_in = false;
    bool has_out = false;
    for (const auto& v : node_arr) {
        const QString type = v.toObject()["type"].toString();
        has_in = has_in || type == "input";
        has_out = has_out || type == "output";
    }
    if (!has_in || !has_out) {
        return false;
    }

    clear();
    bool placed = true; // every node has a saved position (older files don't)
    vector<int> dropped; // nodes this build doesn't have (the Tuner, global since 2026-10-18)
    for (const auto& v : node_arr) {
        const QJsonObject o = v.toObject();
        const int id = o["id"].toInt() > 0 && !node(o["id"].toInt())
                           ? create(o["type"].toString(), o["stereo"].toBool(), o["id"].toInt())
                           : 0;
        Node* n = id ? find(id) : nullptr;
        if (!n) {
            dropped.push_back(o["id"].toInt());
            if (warnings && o["type"].toString() != "tuner") {
                warnings->append(QString("A node of an unknown kind (\"%1\") was left out.")
                                     .arg(o["type"].toString()));
            }
            continue;
        }
        if (o["name"].isString() && !o["name"].toString().trimmed().isEmpty()) {
            n->name = o["name"].toString().trimmed();
        }
        n->enabled = o["enabled"].toBool(true);
        if (o["x"].isDouble() && o["y"].isDouble()) {
            n->pos = QPointF(o["x"].toDouble(), o["y"].toDouble());
        } else {
            placed = false;
        }
        const QJsonObject params = o["params"].toObject();
        for (const auto& p : n->type->params) {
            const QString key = QString::fromStdString(p.key);
            if (params[key].isDouble()) {
                n->values[key] = std::clamp(params[key].toDouble(), p.min, p.max);
            } else if (params[key].isBool()) {
                n->values[key] = params[key].toBool() ? 1.0 : 0.0;
            }
        }
        apply_all(*n);
        const QString file = o["file"].toString();
        if (!file.isEmpty()) {
            if (auto ok = load_file(id, file); !ok && warnings) {
                warnings->append(legacy_file_error(n->name, file, ok.error()));
            }
        }
    }
    vector<GraphWire> saved;
    for (const auto& v : graph["wires"].toArray()) {
        const QJsonObject o = v.toObject();
        saved.push_back(
            {o["from"].toInt(), o["fromPort"].toInt(), o["to"].toInt(), o["toPort"].toInt()});
    }
    auto is_dropped = [&dropped](int id) {
        return std::find(dropped.begin(), dropped.end(), id) != dropped.end();
    };
    // A dropped node's source feeds on to what it fed, so the chain stays joined.
    auto source_of = [&](GraphWire w) -> std::optional<GraphWire> {
        for (int hops = 0; is_dropped(w.from_node) && hops < 64; ++hops) {
            const auto in = std::find_if(saved.begin(), saved.end(), [&w](const GraphWire& x) {
                return x.to_node == w.from_node && x.to_port == 0;
            });
            if (in == saved.end()) {
                return std::nullopt;
            }
            w.from_node = in->from_node;
            w.from_port = in->from_port;
        }
        return is_dropped(w.from_node) ? std::nullopt : std::optional<GraphWire>(w);
    };
    for (const GraphWire& original : saved) {
        if (is_dropped(original.to_node)) {
            continue;
        }
        const std::optional<GraphWire> w = source_of(original);
        if (!w) {
            continue;
        }
        if (can_add_wire(*w)) {
            wires.push_back(*w);
        } else if (warnings) {
            warnings->append("A wire that would break the graph was skipped.");
        }
    }
    push();
    if (!placed || !dropped.empty() || is_organized()) {
        lay_out(); // the last keeps the wire lanes of a rig saved as organized
    }
    emit structure_changed();
    emit modified();
    return true;
}

bool GraphModel::from_v2(const QJsonObject& obj, QStringList* warnings) {
    static const char* const DEFAULT_ORDER[] = {
        "tuner", "transpose", "gate", "comp",   "wah",     "flanger", "phaser", "drive",
        "nam",   "volume",    "eq",   "chorus", "tremolo", "delay",   "reverb", "ir",
    };
    QStringList order;
    for (const auto& v : obj["effectOrder"].toArray()) {
        order << v.toString();
    }
    for (const char* id : DEFAULT_ORDER) {
        if (!order.contains(id)) {
            order << id; // the v2 sanitizer's rule, roughly: missing effects run too
        }
    }

    clear();
    const int in = create("input", false);
    find(in)->values["gain"] = obj["inputGain"].toDouble(0.0);
    apply_all(*find(in));

    // The signal so far: left (or mono) source, and the right one once stereo.
    std::pair<int, int> left{in, 0};
    std::optional<std::pair<int, int>> right;

    auto fill = [&obj](Node& n, const QString& group) {
        const QJsonObject g = obj[group].toObject();
        for (const auto& p : n.type->params) {
            const QString key = QString::fromStdString(p.key);
            if (g[key].isDouble()) {
                n.values[key] = std::clamp(g[key].toDouble(), p.min, p.max);
            } else if (g[key].isBool()) {
                n.values[key] = g[key].toBool() ? 1.0 : 0.0;
            }
        }
    };

    for (const QString& id : order) {
        const NodeType* type = NodeCatalog::find(id.toStdString());
        if (!type || id == "input" || id == "output" || id == "merge") {
            continue;
        }
        const QString group = QString::fromStdString(NodeCatalog::legacy_group(id.toStdString()));
        bool enabled = obj[group].toObject()["enabled"].toBool(false);
        if (id == "nam") {
            enabled = obj["namEnabled"].toBool(true);
        } else if (id == "ir") {
            enabled = obj["irEnabled"].toBool(true);
        } else if (id == "eq") {
            enabled = obj[group].toObject()["enabled"].toBool(true);
        }
        // Amp and cab stay (bypassed if they were off); other effects only when on.
        if (!enabled && id != "nam" && id != "ir") {
            continue;
        }
        const bool stereo_switch =
            id == "reverb" || obj[group].toObject()["stereo"].toBool(id == "delay");
        // Mono in → stereo out only while the signal is still mono; once it is stereo,
        // every effect runs once per side (the chain's per-channel path): two mono nodes.
        // `wide`: the effect ran mono in → stereo out. A stereo-out-only type that ran mono
        // still gets its one node, with only its L output wired.
        const bool wide = !right && type->has_stereo && stereo_switch;
        const bool stereo = type->variant(wide);
        const QString file = id == "nam"  ? obj["namPath"].toString()
                             : id == "ir" ? obj["irPath"].toString()
                                          : QString();

        auto make = [&] {
            const int nid = create(id, stereo);
            Node* n = find(nid);
            fill(*n, group);
            n->enabled = enabled;
            apply_all(*n);
            if (!file.isEmpty()) {
                if (auto ok = load_file(nid, file); !ok && warnings) {
                    warnings->append(legacy_file_error(n->name, file, ok.error()));
                }
            }
            return nid;
        };
        if (wide) {
            const int nid = make();
            wires.push_back({left.first, left.second, nid, 0});
            left = {nid, 0};
            right = std::pair{nid, 1};
        } else if (right) {
            // After a stereo node the chain ran each effect once per channel: two nodes.
            const int l = make();
            const int r = make();
            find(l)->name = unique_name(find(l)->name + " L");
            find(r)->name = unique_name(QString::fromStdString(type->name) + " R");
            wires.push_back({left.first, left.second, l, 0});
            wires.push_back({right->first, right->second, r, 0});
            left = {l, 0};
            right = std::pair{r, 0};
        } else {
            const int nid = make();
            wires.push_back({left.first, left.second, nid, 0});
            left = {nid, 0};
        }
    }

    const int out = create("output", false);
    Node* o = find(out);
    o->values["gain"] = obj["outputGain"].toDouble(0.0);
    o->values["limiter"] = obj["limiter"].toBool(false) ? 1.0 : 0.0;
    apply_all(*o);
    wires.push_back({left.first, left.second, out, 0});
    if (right) {
        wires.push_back({right->first, right->second, out, 1});
    }
    push();
    lay_out();
    emit structure_changed();
    emit modified();
    return true;
}

} // namespace namx
