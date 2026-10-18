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
class QMenu;
class QStackedLayout;

namespace namx
{

class AudioEngine;
class GraphModel;
class KnobWidget;
class ToggleSwitch;
class VUMeter;
struct ParamSpec;

// The right-hand panel (ui/node-graph): the selected node's header — icon, name, its
// I/O, the power switch, a menu — and every parameter of its type, built from the
// NodeCatalog. Amp and Cab nodes add their file, Input and
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
    // A Meter node's peak (linear) since the last poll, shown while it's selected.
    void set_meter_peak(int id, float peak);

    // Hooks the window provides (the node menu's Duplicate / Delete select or clear).
    std::function<void(int)> on_duplicate;
    // Adds the MIDI Learn / Forget entries for a target ID to a context menu. Every
    // control carries its target in the "midiTarget" property (knobs get their menu
    // through KnobWidget's hook).
    std::function<void(const QString&, QMenu*)> learn_menu;

private:
    void sync_from_model(int id); // values, power and file without a rebuild
    void build_header(QVBoxLayout* lay);
    void build_file_section(QVBoxLayout* lay);
    void build_params(QVBoxLayout* lay);
    QWidget* make_switch(const ParamSpec& p);
    QWidget* make_choice(const ParamSpec& p);
    KnobWidget* make_knob(const ParamSpec& p);
    // A knob, or for a synced one the knob and its note value in one place.
    QWidget* make_dial_cell(const ParamSpec& p);
    // MIDI target ID of the current node's `key` (GraphModel::midi_target_id).
    QString target_for(const QString& key) const;
    // Right-click on `w`: MIDI Learn for `key`, through learn_menu.
    void offer_learn(QWidget* w, const QString& key);
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
    struct SyncedCell
    {
        QStackedLayout* stack; // 0 = the knob, 1 = its note value
        QString sync_key;
    };
    vector<SyncedCell> synced_cells;
    VUMeter* meter = nullptr;
};

} // namespace namx
