#pragma once

#include <functional>

#include <QHash>
#include <QPointer>
#include <QWidget>

#include "Common.h"
#include "LibraryIndex.h"

class QLabel;
class QToolButton;
class QVBoxLayout;
class QGridLayout;

namespace namx
{

class AudioEngine;
class GraphModel;
class KnobWidget;
class ToggleSwitch;
class TunerDisplay;
class VUMeter;
struct ParamSpec;

// The right-hand panel (ui/node-graph): the selected node's header — icon, name, its
// I/O, the power switch, a menu — and every parameter of its type, built from the
// NodeCatalog. Amp and Cab nodes add their file, the Tuner its display, Input and
// Output their level meters.
class ParamPanel : public QWidget
{
    Q_OBJECT

public:
    ParamPanel(GraphModel& new_model, LibraryIndex& new_library, QWidget* parent = nullptr);

    // Shows `id`'s controls (0 = the empty state). Rebuilt every time: cheap, and a
    // node's variant (mono / stereo) may have changed under the same ID.
    void show_node(int id);
    int node_id() const {
        return current_id;
    }

    // Live readouts, from the window's timers.
    void poll_meters(AudioEngine& engine);
    void poll_tuner(int reference_hz);

    // Hooks the window provides (the node menu's Duplicate / Delete select or clear).
    std::function<void(int)> on_duplicate;

private:
    void sync_from_model(int id); // values, power and file without a rebuild
    void build_header(QVBoxLayout* lay);
    void build_file_section(QVBoxLayout* lay);
    void build_params(QVBoxLayout* lay);
    QWidget* make_param(const ParamSpec& p);
    void browse_file();
    void step_file(int step);
    void update_file_labels();
    QString io_text() const;

    GraphModel& model;
    LibraryIndex& library;
    int current_id = 0;
    QWidget* body = nullptr; // rebuilt per node
    QLabel* title = nullptr;
    QLabel* subtitle = nullptr;
    QToolButton* power = nullptr;
    QLabel* file_name = nullptr;
    QLabel* file_meta = nullptr;
    QHash<QString, KnobWidget*> knobs;
    QHash<QString, ToggleSwitch*> switches;
    QHash<QString, QList<QToolButton*>> choices;
    TunerDisplay* tuner = nullptr;
    VUMeter* meter = nullptr;
};

} // namespace namx
