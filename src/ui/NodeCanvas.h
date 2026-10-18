#pragma once

#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include <QGraphicsView>

#include "Common.h"
#include "SignalGraph.h"

class QMenu;
class QToolButton;
class QGraphicsPathItem;

namespace namx
{

class GraphModel;
class NodeItem;
class WireItem;

// The node network (ui/node-graph): one card per node, signal flowing top to bottom
// — inputs on a card's top edge, outputs on its bottom edge — and a wire per
// connection. Cards sit where GraphModel's layout puts them; they can't be dragged. Drag from a port to a port to connect (either direction); a wire into an
// input that already has one replaces it, and a wire that would close a loop is
// refused (drawn red while dragging). Drag a wired input away to move or drop its wire. Right-click for menus; Delete removes the
// selection; the wheel zooms; dragging the background pans.
class NodeCanvas : public QGraphicsView
{
    Q_OBJECT

public:
    explicit NodeCanvas(GraphModel& new_model, QWidget* parent = nullptr);

    // The Add menu, by category: `pick(type, stereo)` for the chosen entry.
    static void populate_add_menu(QMenu* menu,
                                  const std::function<void(const QString&, bool)>& pick);

    void select_node(int id);
    int selected_node() const;
    void zoom_by(double factor);
    void fit_all();

signals:
    // The selected node (0 = none).
    void node_selected(int id);

protected:
    void wheelEvent(QWheelEvent* ev) override;
    void keyPressEvent(QKeyEvent* ev) override;
    void contextMenuEvent(QContextMenuEvent* ev) override;
    void mouseDoubleClickEvent(QMouseEvent* ev) override;
    void drawBackground(QPainter* p, const QRectF& rect) override;
    void resizeEvent(QResizeEvent* ev) override;

private:
    friend class NodeItem;

    void rebuild();
    void hide_wire(const GraphWire& wire); // picked up: hidden until the rebuild
    void refresh_node(int id);
    void update_wires();
    void show_node_menu(int id, const QPoint& global_pos);
    void show_wire_menu(const GraphWire& wire, const QPoint& global_pos);
    void show_add_menu(const QPoint& global_pos);
    void delete_selection();

    // Wire drawing, driven by the card that got the press. `from_output`: the fixed
    // end is an output port (the free end looks for an input), else the other way.
    void begin_wire(int node, int port, bool from_output);
    void drag_wire(QPointF scene_pos);
    void end_wire(QPointF scene_pos);
    // The port under `scene_pos` on the side opposite the fixed end, if any.
    std::optional<std::pair<int, int>> port_at(QPointF scene_pos, bool want_input) const;

    GraphModel& model;
    QGraphicsScene* scene;
    std::unordered_map<int, NodeItem*> node_items;
    vector<WireItem*> wire_items;
    QGraphicsPathItem* drag_path = nullptr;
    struct WireDrag
    {
        int node;
        int port;
        bool from_output;
    };
    std::optional<WireDrag> wire_drag;
    bool rebuild_pending = false;
    QWidget* zoom_box = nullptr;
};

} // namespace namx
