#include "NodeCanvas.h"

#include <algorithm>
#include <cmath>

#include <QContextMenuEvent>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPathStroker>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "GraphLayout.h"
#include "GraphModel.h"
#include "NodeCatalog.h"
#include "NodeIcons.h"
#include "Theme.h"

namespace namx
{

namespace
{

using GraphLayout::NODE_H;
using GraphLayout::NODE_W;
using GraphLayout::port_x;
constexpr double PORT_R = 5.5;
constexpr double PORT_HIT = 11.0; // grab radius around a port
constexpr double MIN_ZOOM = 0.3;
constexpr double MAX_ZOOM = 2.5;

// The theme accent of a node's type (Input/Output take the app accent).
QColor node_accent(const GraphModel::Node& n) {
    const string& id = n.type->id;
    return Theme::fx_accent(id == "input" || id == "output" ? QString("neutral")
                                                            : QString::fromStdString(id));
}

// "L" / "R", "A" / "B" for two-port sides; nothing for one.
QString port_label(NodeKind kind, bool input, int port) {
    if (kind == NodeKind::MERGE && input) {
        return port == 0 ? "A" : "B";
    }
    const int count = input ? input_count(kind) : output_count(kind);
    if (count < 2) {
        return {};
    }
    return port == 0 ? "L" : "R";
}

// A vertical S-curve from an output port down to an input port.
void curve_to(QPainterPath& path, QPointF b) {
    const QPointF a = path.currentPosition();
    const double dy = std::max(24.0, std::abs(b.y() - a.y()) / 2.0);
    path.cubicTo(a + QPointF(0, dy), b - QPointF(0, dy), b);
}

QPainterPath wire_path(QPointF a, QPointF b) {
    QPainterPath path(a);
    curve_to(path, b);
    return path;
}

// Through the lanes of the rows it skips: (top, bottom) pairs, straight down each lane.
QPainterPath routed_path(QPointF a, const vector<QPointF>& lanes, QPointF b) {
    QPainterPath path(a);
    for (size_t i = 0; i + 1 < lanes.size(); i += 2) {
        curve_to(path, lanes[i]);
        path.lineTo(lanes[i + 1]);
    }
    curve_to(path, b);
    return path;
}

} // namespace

// One node's card.
class NodeItem : public QGraphicsItem
{
public:
    NodeItem(NodeCanvas& new_canvas, int new_id)
        : id(new_id)
        , canvas(new_canvas) {
        setFlags(ItemIsSelectable); // placed by the layout, never dragged
        setAcceptHoverEvents(true);
    }

    QRectF boundingRect() const override {
        return QRectF(-10, -18, NODE_W + 20, NODE_H + 36);
    }

    QPointF port_pos(bool input, int port) const {
        const GraphModel::Node* n = canvas.model.node(id);
        const int count = input ? input_count(n->kind()) : output_count(n->kind());
        return mapToScene(QPointF(port_x(count, port), input ? 0.0 : NODE_H));
    }

    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override {
        const GraphModel::Node* n = canvas.model.node(id);
        if (!n) {
            return;
        }
        p->setRenderHint(QPainter::Antialiasing);
        const QColor accent = node_accent(*n);
        // Bypassed, or out of the signal path (a dead end or an unfed branch, which the
        // engine doesn't run — dashed): drawn faded.
        const bool in_path = n->kind() == NodeKind::INPUT || n->kind() == NodeKind::OUTPUT ||
                             canvas.model.is_live(id);
        const bool live = n->enabled && in_path;
        const QRectF body(0, 0, NODE_W, NODE_H);

        // Glow when selected.
        if (isSelected()) {
            for (int i = 3; i >= 1; --i) {
                QColor g = accent;
                g.setAlpha(28 * (4 - i));
                p->setPen(QPen(g, 2.0 * i + 2.0));
                p->setBrush(Qt::NoBrush);
                p->drawRoundedRect(body, 9, 9);
            }
        }
        QColor edge = accent;
        if (!live) {
            edge = Theme::node_text_dim();
        }
        p->setPen(QPen(edge, isSelected() ? 2.2 : 1.6, in_path ? Qt::SolidLine : Qt::DashLine));
        p->setBrush(Theme::node_fill());
        p->drawRoundedRect(body, 9, 9);

        p->setOpacity(live ? 1.0 : 0.45);
        // Icon box.
        const QRectF icon(12, 12, 48, 48);
        QColor tint = accent;
        tint.setAlpha(40);
        p->setPen(QPen(accent, 1.4));
        p->setBrush(tint);
        p->drawRoundedRect(icon, 7, 7);
        paint_node_icon(*p, icon, QString::fromStdString(n->type->id),
                        QString::fromStdString(n->type->category), accent);

        // Name and I/O line.
        QFont f = p->font();
        f.setPointSizeF(11.5);
        f.setWeight(QFont::DemiBold);
        p->setFont(f);
        p->setPen(Theme::node_text());
        const double text_x = 72;
        const double text_w = NODE_W - text_x - 46; // room for the power button and ⋮
        p->drawText(QRectF(text_x, 12, text_w, 24), Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetricsF(f).elidedText(n->name, Qt::ElideRight, text_w));
        f.setPointSizeF(8.5);
        f.setWeight(QFont::Normal);
        p->setFont(f);
        p->setPen(Theme::node_text_dim());
        p->drawText(QRectF(text_x, 38, text_w + 14, 20), Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetricsF(f).elidedText(io_line(*n), Qt::ElideRight, text_w + 14));
        p->setOpacity(1.0);

        // Bypass button and ⋮ menu affordance.
        if (n->kind() != NodeKind::INPUT && n->kind() != NodeKind::OUTPUT) {
            const QRectF power = power_rect();
            QColor ring = n->enabled ? accent : Theme::node_text_dim();
            p->setPen(QPen(ring, 1.2));
            QColor fill = ring;
            fill.setAlpha(n->enabled ? 40 : 0);
            p->setBrush(fill);
            p->drawEllipse(power);
            paint_power_icon(*p, power.adjusted(3, 3, -3, -3), ring);
            p->setPen(Qt::NoPen);
            p->setBrush(Theme::node_text_dim());
            for (int i = 0; i < 3; ++i) {
                p->drawEllipse(QPointF(NODE_W - 14, 20 + i * 6.0), 1.6, 1.6);
            }
        }

        // Ports.
        for (int side = 0; side < 2; ++side) {
            const bool input = side == 0;
            const int count = input ? input_count(n->kind()) : output_count(n->kind());
            for (int port = 0; port < count; ++port) {
                const QPointF c(port_x(count, port), input ? 0.0 : NODE_H);
                p->setPen(QPen(accent, 1.6));
                p->setBrush(wired(input, port) ? QBrush(accent) : QBrush(Theme::node_fill()));
                p->drawEllipse(c, PORT_R, PORT_R);
                const QString label = port_label(n->kind(), input, port);
                if (!label.isEmpty()) {
                    QFont lf = p->font();
                    lf.setPointSizeF(7.5);
                    lf.setWeight(QFont::DemiBold);
                    p->setFont(lf);
                    p->setPen(Theme::node_text_dim());
                    p->drawText(QRectF(c.x() + 7, input ? c.y() - 16 : c.y() + 2, 16, 14),
                                Qt::AlignLeft | Qt::AlignVCenter, label);
                }
            }
        }
    }

    const int id;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* ev) override {
        if (ev->button() == Qt::LeftButton) {
            if (const auto hit = port_hit(ev->pos())) {
                const auto [input, port] = *hit;
                wiring = true;
                if (input) {
                    // A wired input: pick its wire up from the source end.
                    for (const auto& w : canvas.model.get_wires()) {
                        if (w.to_node == id && w.to_port == port) {
                            const GraphWire old = w;
                            canvas.hide_wire(old);
                            canvas.model.remove_wire(old);
                            canvas.begin_wire(old.from_node, old.from_port, true);
                            canvas.drag_wire(ev->scenePos());
                            return;
                        }
                    }
                    canvas.begin_wire(id, port, false);
                } else {
                    canvas.begin_wire(id, port, true);
                }
                canvas.drag_wire(ev->scenePos());
                return;
            }
            const GraphModel::Node* n = canvas.model.node(id);
            if (n && n->kind() != NodeKind::INPUT && n->kind() != NodeKind::OUTPUT &&
                power_rect().adjusted(-3, -3, 3, 3).contains(ev->pos())) {
                canvas.model.set_enabled(id, !n->enabled); // the card's bypass button
                ev->accept();
                return;
            }
            if (n && n->kind() != NodeKind::INPUT && n->kind() != NodeKind::OUTPUT &&
                QRectF(NODE_W - 24, 8, 20, 30).contains(ev->pos())) {
                canvas.select_node(id);
                // After this handler: a menu action may rebuild the scene (and this card).
                const QPoint at = ev->screenPos();
                QTimer::singleShot(0, &canvas, [c = &canvas, nid = id, at] {
                    c->show_node_menu(nid, at);
                });
                ev->accept();
                return;
            }
        }
        QGraphicsItem::mousePressEvent(ev);
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent* ev) override {
        if (wiring) {
            canvas.drag_wire(ev->scenePos());
            return;
        }
        QGraphicsItem::mouseMoveEvent(ev);
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* ev) override {
        if (wiring) {
            wiring = false;
            canvas.end_wire(ev->scenePos());
            return;
        }
        QGraphicsItem::mouseReleaseEvent(ev);
    }

    void hoverMoveEvent(QGraphicsSceneHoverEvent* ev) override {
        const GraphModel::Node* n = canvas.model.node(id);
        const bool on_power = n && n->kind() != NodeKind::INPUT && n->kind() != NodeKind::OUTPUT &&
                              power_rect().adjusted(-3, -3, 3, 3).contains(ev->pos());
        setCursor(port_hit(ev->pos()) ? Qt::CrossCursor
                  : on_power          ? Qt::PointingHandCursor
                                      : Qt::ArrowCursor);
        setToolTip(on_power
                       ? QString(n->enabled ? "On: click to bypass" : "Bypassed: click to turn on")
                       : QString());
    }

private:
    // The bypass button, left of the ⋮.
    static QRectF power_rect() {
        return QRectF(NODE_W - 44, 13, 20, 20);
    }

    // (input?, port) under a local point.
    std::optional<std::pair<bool, int>> port_hit(QPointF local) const {
        const GraphModel::Node* n = canvas.model.node(id);
        if (!n) {
            return std::nullopt;
        }
        for (int side = 0; side < 2; ++side) {
            const bool input = side == 0;
            const int count = input ? input_count(n->kind()) : output_count(n->kind());
            for (int port = 0; port < count; ++port) {
                const QPointF c(port_x(count, port), input ? 0.0 : NODE_H);
                if (QLineF(c, local).length() <= PORT_HIT) {
                    return std::pair{input, port};
                }
            }
        }
        return std::nullopt;
    }

    bool wired(bool input, int port) const {
        return std::any_of(canvas.model.get_wires().begin(), canvas.model.get_wires().end(),
                           [this, input, port](const GraphWire& w) {
                               return input ? (w.to_node == id && w.to_port == port)
                                            : (w.from_node == id && w.from_port == port);
                           });
    }

    // "Mono → Stereo", "Mono (Guitar)", or the loaded file for Amp/Cab.
    QString io_line(const GraphModel::Node& n) const {
        if (n.type->file != NodeType::File::NONE) {
            return n.file.isEmpty()
                       ? QString(n.type->file == NodeType::File::MODEL ? "No model loaded"
                                                                       : "No IR loaded")
                       : QFileInfo(n.file).completeBaseName();
        }
        switch (n.kind()) {
            case NodeKind::INPUT:
                return "Mono (Guitar)";
            case NodeKind::OUTPUT:
                return wired(true, 0) && wired(true, 1) ? "Stereo  L · R" : "Mono → L + R";
            case NodeKind::MERGE:
                return "2 × Mono → Mono";
            case NodeKind::STEREO:
                return "Mono → Stereo";
            case NodeKind::MONO:
                break;
        }
        return "Mono → Mono";
    }

    NodeCanvas& canvas;
    bool wiring = false;
};

// One connection.
class WireItem : public QGraphicsPathItem
{
public:
    WireItem(const GraphWire& new_wire, vector<QPointF> new_lanes, const QColor& new_color)
        : wire(new_wire)
        , lanes(std::move(new_lanes))
        , color(new_color) {
        setFlags(ItemIsSelectable);
        setAcceptHoverEvents(true);
        setZValue(-1);
    }

    QPainterPath shape() const override {
        QPainterPathStroker s;
        s.setWidth(12);
        return s.createStroke(path());
    }

    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override {
        p->setRenderHint(QPainter::Antialiasing);
        const bool hot = isSelected() || hovered;
        QColor c = color;
        c.setAlpha(hot ? 255 : 200);
        p->setPen(QPen(c, hot ? 3.4 : 2.2, Qt::SolidLine, Qt::RoundCap));
        p->setBrush(Qt::NoBrush);
        p->drawPath(path());
    }

    const GraphWire wire;
    const vector<QPointF> lanes; // GraphModel::route

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent*) override {
        hovered = true;
        update();
    }
    void hoverLeaveEvent(QGraphicsSceneHoverEvent*) override {
        hovered = false;
        update();
    }

private:
    QColor color;
    bool hovered = false;
};

NodeCanvas::NodeCanvas(GraphModel& new_model, QWidget* parent)
    : QGraphicsView(parent)
    , model(new_model)
    , scene(new QGraphicsScene(this)) {
    setObjectName("nodeCanvas");
    setScene(scene);
    scene->setSceneRect(-5000, -5000, 10000, 10000);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setDragMode(ScrollHandDrag);
    setTransformationAnchor(AnchorUnderMouse);
    setViewportUpdateMode(FullViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);

    // Zoom controls, top-left over the canvas.
    zoom_box = new QWidget(this);
    zoom_box->setObjectName("canvasTools");
    auto* lay = new QVBoxLayout(zoom_box);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(2);
    auto tool = [this, lay](const QString& text, const QString& tip, auto fn) {
        auto* b = new QToolButton;
        b->setObjectName("canvasTool");
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, fn);
        lay->addWidget(b);
    };
    tool("+", "Zoom in", [this] {
        zoom_by(1.2);
    });
    tool("−", "Zoom out", [this] {
        zoom_by(1.0 / 1.2);
    });
    tool("⛶", "Fit the whole rig", [this] {
        fit_all();
    });
    zoom_box->move(14, 14);

    // Queued: a structure change can come from inside a card's own mouse handler.
    connect(&model, &GraphModel::structure_changed, this, &NodeCanvas::rebuild,
            Qt::QueuedConnection);
    connect(&model, &GraphModel::node_changed, this, &NodeCanvas::refresh_node);
    connect(scene, &QGraphicsScene::selectionChanged, this, [this] {
        emit node_selected(selected_node());
    });
    rebuild();
}

void NodeCanvas::populate_add_menu(QMenu* menu,
                                   const std::function<void(const QString&, bool)>& pick) {
    static const char* const CATEGORIES[] = {"Amp",   "Cab",        "Drive", "Dynamics", "Filter",
                                             "Pitch", "Modulation", "Delay", "Reverb",   "Utility"};
    for (const char* cat : CATEGORIES) {
        QMenu* sub = menu->addMenu(cat);
        for (const auto& t : NodeCatalog::types()) {
            if (t.category != cat) {
                continue;
            }
            const QString id = QString::fromStdString(t.id);
            const QString name = QString::fromStdString(t.name);
            if (t.has_mono) {
                sub->addAction(name, menu, [pick, id] {
                    pick(id, false);
                });
            }
            if (t.has_stereo) {
                sub->addAction(t.has_mono ? name + " (Stereo)" : name, menu, [pick, id] {
                    pick(id, true);
                });
            }
        }
    }
}

void NodeCanvas::hide_wire(const GraphWire& wire) {
    for (WireItem* w : wire_items) {
        if (w->wire == wire) {
            w->hide();
        }
    }
}

void NodeCanvas::rebuild() {
    // Not under a wire being drawn: the card that holds the mouse would go.
    if (wire_drag) {
        rebuild_pending = true;
        return;
    }
    rebuild_pending = false;
    const int selected = selected_node();
    drag_path = nullptr;
    node_items.clear();
    wire_items.clear();
    {
        const QSignalBlocker block(scene);
        scene->clear();
    }
    for (const auto& n : model.get_nodes()) {
        auto* item = new NodeItem(*this, n.id());
        item->setPos(n.pos);
        scene->addItem(item);
        node_items[n.id()] = item;
    }
    for (size_t i = 0; i < model.get_wires().size(); ++i) {
        const GraphWire& w = model.get_wires()[i];
        const GraphModel::Node* from = model.node(w.from_node);
        auto* item =
            new WireItem(w, model.route(i), from ? node_accent(*from) : Theme::wire_drag());
        scene->addItem(item);
        wire_items.push_back(item);
    }
    update_wires();
    if (const auto it = node_items.find(selected); it != node_items.end()) {
        const QSignalBlocker block(scene);
        it->second->setSelected(true);
    }
    emit node_selected(selected_node());
}

void NodeCanvas::refresh_node(int id) {
    if (const auto it = node_items.find(id); it != node_items.end()) {
        it->second->update();
    }
}

void NodeCanvas::update_wires() {
    for (WireItem* w : wire_items) {
        const auto from = node_items.find(w->wire.from_node);
        const auto to = node_items.find(w->wire.to_node);
        if (from == node_items.end() || to == node_items.end()) {
            continue;
        }
        w->setPath(routed_path(from->second->port_pos(false, w->wire.from_port), w->lanes,
                               to->second->port_pos(true, w->wire.to_port)));
    }
}

void NodeCanvas::select_node(int id) {
    const auto it = node_items.find(id);
    if (it == node_items.end()) {
        return;
    }
    scene->clearSelection();
    it->second->setSelected(true);
    ensureVisible(it->second, 40, 40);
}

int NodeCanvas::selected_node() const {
    for (QGraphicsItem* item : scene->selectedItems()) {
        if (auto* n = dynamic_cast<NodeItem*>(item)) {
            return n->id;
        }
    }
    return 0;
}

void NodeCanvas::zoom_by(double factor) {
    const double current = transform().m11();
    factor = std::clamp(current * factor, MIN_ZOOM, MAX_ZOOM) / current;
    scale(factor, factor);
}

void NodeCanvas::fit_all() {
    QRectF r;
    for (const auto& [id, item] : node_items) {
        r |= item->sceneBoundingRect();
    }
    if (r.isEmpty()) {
        return;
    }
    fitInView(r.adjusted(-60, -60, 60, 60), Qt::KeepAspectRatio);
    // Never blow a small rig up past 1:1.
    if (transform().m11() > 1.0) {
        const QPointF c = r.center();
        resetTransform();
        centerOn(c);
    }
}

void NodeCanvas::wheelEvent(QWheelEvent* ev) {
    zoom_by(std::pow(1.0015, ev->angleDelta().y()));
    ev->accept();
}

void NodeCanvas::keyPressEvent(QKeyEvent* ev) {
    if (ev->key() == Qt::Key_Delete || ev->key() == Qt::Key_Backspace) {
        delete_selection();
        ev->accept();
        return;
    }
    QGraphicsView::keyPressEvent(ev);
}

void NodeCanvas::delete_selection() {
    vector<GraphWire> wires;
    vector<int> nodes;
    for (QGraphicsItem* item : scene->selectedItems()) {
        if (auto* w = dynamic_cast<WireItem*>(item)) {
            wires.push_back(w->wire);
        } else if (auto* n = dynamic_cast<NodeItem*>(item)) {
            nodes.push_back(n->id);
        }
    }
    for (const auto& w : wires) {
        model.remove_wire(w);
    }
    for (const int id : nodes) {
        model.remove_node(id);
    }
}

void NodeCanvas::contextMenuEvent(QContextMenuEvent* ev) {
    QGraphicsItem* item = itemAt(ev->pos());
    if (auto* n = dynamic_cast<NodeItem*>(item)) {
        select_node(n->id);
        show_node_menu(n->id, ev->globalPos());
    } else if (auto* w = dynamic_cast<WireItem*>(item)) {
        show_wire_menu(w->wire, ev->globalPos());
    } else {
        show_add_menu(ev->globalPos());
    }
    ev->accept();
}

void NodeCanvas::mouseDoubleClickEvent(QMouseEvent* ev) {
    if (!itemAt(ev->pos())) {
        show_add_menu(ev->globalPosition().toPoint());
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(ev);
}

void NodeCanvas::show_add_menu(const QPoint& global_pos) {
    QMenu menu(this);
    menu.addSection("Add Node");
    populate_add_menu(&menu, [this](const QString& type, bool stereo) {
        if (const int id = model.add_node(type, stereo)) {
            QTimer::singleShot(0, this, [this, id] {
                select_node(id);
            });
        }
    });
    menu.exec(global_pos);
}

void NodeCanvas::show_node_menu(int id, const QPoint& global_pos) {
    const GraphModel::Node* n = model.node(id);
    if (!n) {
        return;
    }
    const bool io = n->kind() == NodeKind::INPUT || n->kind() == NodeKind::OUTPUT;
    QMenu menu(this);
    menu.addAction("Rename…", this, [this, id] {
        const GraphModel::Node* node = model.node(id);
        bool ok = false;
        const QString name = QInputDialog::getText(this, "Rename Node", "Name:", QLineEdit::Normal,
                                                   node ? node->name : QString(), &ok);
        if (ok) {
            model.set_name(id, name);
        }
    });
    if (!io) {
        QAction* on = menu.addAction("Enabled", this, [this, id](bool checked) {
            model.set_enabled(id, checked);
        });
        on->setCheckable(true);
        on->setChecked(n->enabled);
        if (n->kind() == NodeKind::MONO && n->type->has_stereo) {
            menu.addAction("Make Stereo", this, [this, id] {
                model.set_stereo(id, true);
            });
        } else if (n->kind() == NodeKind::STEREO && n->type->has_mono) {
            menu.addAction("Make Mono", this, [this, id] {
                model.set_stereo(id, false);
            });
        }
        menu.addAction("Duplicate", this, [this, id] {
            if (const int copy = model.duplicate_node(id)) {
                QTimer::singleShot(0, this, [this, copy] {
                    select_node(copy);
                });
            }
        });
        menu.addSeparator();
        menu.addAction("Delete", this, [this, id] {
            model.remove_node(id);
        });
    }
    menu.exec(global_pos);
}

void NodeCanvas::show_wire_menu(const GraphWire& wire, const QPoint& global_pos) {
    QMenu menu(this);
    QMenu* insert = menu.addMenu("Insert Node");
    populate_add_menu(insert, [this, wire](const QString& type, bool stereo) {
        const int id = model.add_node(type, stereo);
        if (id && model.insert_on_wire(id, wire)) {
            QTimer::singleShot(0, this, [this, id] {
                select_node(id);
            });
        }
    });
    menu.addAction("Delete Wire", this, [this, wire] {
        model.remove_wire(wire);
    });
    menu.exec(global_pos);
}

void NodeCanvas::begin_wire(int node, int port, bool from_output) {
    wire_drag = WireDrag{node, port, from_output};
    if (!drag_path) {
        drag_path = scene->addPath(QPainterPath(), QPen(Theme::wire_drag(), 2.0, Qt::DashLine));
        drag_path->setZValue(10);
    }
}

void NodeCanvas::drag_wire(QPointF scene_pos) {
    if (!wire_drag || !drag_path) {
        return;
    }
    const auto it = node_items.find(wire_drag->node);
    if (it == node_items.end()) {
        return;
    }
    const QPointF fixed = it->second->port_pos(!wire_drag->from_output, wire_drag->port);
    QPointF free_end = scene_pos;
    bool ok = true;
    if (const auto target = port_at(scene_pos, wire_drag->from_output)) {
        free_end = node_items[target->first]->port_pos(wire_drag->from_output, target->second);
        const GraphWire w =
            wire_drag->from_output
                ? GraphWire{wire_drag->node, wire_drag->port, target->first, target->second}
                : GraphWire{target->first, target->second, wire_drag->node, wire_drag->port};
        ok = model.can_connect(w); // red: it would make a loop
    }
    drag_path->setPen(QPen(ok ? Theme::wire_drag() : Theme::status_bad(), 2.0, Qt::DashLine));
    drag_path->setPath(wire_drag->from_output ? wire_path(fixed, free_end)
                                              : wire_path(free_end, fixed));
}

void NodeCanvas::end_wire(QPointF scene_pos) {
    if (drag_path) {
        scene->removeItem(drag_path);
        delete drag_path;
        drag_path = nullptr;
    }
    if (!wire_drag) {
        return;
    }
    const WireDrag d = *wire_drag;
    wire_drag.reset();
    if (const auto target = port_at(scene_pos, d.from_output)) {
        const GraphWire w = d.from_output
                                ? GraphWire{d.node, d.port, target->first, target->second}
                                : GraphWire{target->first, target->second, d.node, d.port};
        model.connect(w); // replaces the input's old wire; refused if it makes a loop
    }
    if (rebuild_pending) {
        QTimer::singleShot(0, this, &NodeCanvas::rebuild);
    }
}

std::optional<std::pair<int, int>> NodeCanvas::port_at(QPointF scene_pos, bool want_input) const {
    for (const auto& [id, item] : node_items) {
        const GraphModel::Node* n = model.node(id);
        if (!n) {
            continue;
        }
        const int count = want_input ? input_count(n->kind()) : output_count(n->kind());
        for (int port = 0; port < count; ++port) {
            if (QLineF(item->port_pos(want_input, port), scene_pos).length() <= PORT_HIT * 1.6) {
                return std::pair{id, port};
            }
        }
    }
    return std::nullopt;
}

void NodeCanvas::drawBackground(QPainter* p, const QRectF& rect) {
    p->fillRect(rect, Theme::canvas_bg());
    constexpr double STEP = 24.0;
    p->setPen(QPen(Theme::canvas_dot(), 1.6, Qt::SolidLine, Qt::RoundCap));
    const double x0 = std::floor(rect.left() / STEP) * STEP;
    const double y0 = std::floor(rect.top() / STEP) * STEP;
    for (double x = x0; x < rect.right(); x += STEP) {
        for (double y = y0; y < rect.bottom(); y += STEP) {
            p->drawPoint(QPointF(x, y));
        }
    }
}

void NodeCanvas::resizeEvent(QResizeEvent* ev) {
    QGraphicsView::resizeEvent(ev);
    zoom_box->adjustSize();
}

} // namespace namx
