#pragma once

#include <vector>

#include <QJsonObject>
#include <QMainWindow>
#include <QString>

#include "Common.h"
#include "TopBar.h"

class QFrame;
class QLabel;
class QMenu;
class QShortcut;
class QSplitter;
class QTimer;

namespace namx
{

class AudioEngine;
class AudioStatusBar;
class GraphModel;
class LibraryIndex;
class MidiInput;
class MidiMapper;
class NodeCanvas;
class NumberField;
class ParamPanel;
class SettingsDialog;
class TunerDisplay;
class ToggleSwitch;

// The main window of the node-graph UI (ui/node-graph): the top bar, the node
// network on the left, the selected node's parameters on the right, the audio status
// bar at the bottom. Owns the engine and the graph model; presets and the session
// are the model's JSON (format 3; v2 presets and the chain-era session load through
// GraphModel's migration).
class RigWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit RigWindow(QWidget* parent = nullptr);
    ~RigWindow() override;

    // Command line / file manager: a preset loads; a .nam / .wav goes into the selected
    // Amp / Cab node, else into the first one, else into a new node.
    void open_paths(const QStringList& paths);
    // Running as an AppImage that no launcher starts yet: offer once to add it to the
    // applications menu.
    void offer_desktop_integration();

protected:
    void closeEvent(QCloseEvent* ev) override;
    void dragEnterEvent(QDragEnterEvent* ev) override;
    void dropEvent(QDropEvent* ev) override;

private:
    void setup_ui();
    void setup_menus();
    void setup_audio();
    void restart_audio();
    void restore_connections();
    void save_connections();
    void update_status();
    void set_notice(const QString& key, const QString& text, const QString& tip = {});
    void open_settings();

    // MIDI: input, actions, learn, status light, menu.
    void setup_midi();
    // Every node parameter as a MIDI target, by node name (GraphModel::midi_target_id);
    // again after each structure change. Chain-era IDs alias the first node of a type.
    void register_midi_targets();
    void add_learn_entries(const QString& target_id, QMenu* menu);
    void on_learn_changed();
    void show_midi_light(bool lit);
    void open_midi_mappings();

    // The global tuner (not a node): the strip under the top bar.
    QFrame* make_tuner_strip();
    void set_tuner_on(bool on);
    void poll_tuner();

    // Files into nodes.
    void load_file_into_node(const QString& path, bool is_model);

    // Presets.
    bool load_preset_file(const QString& path);
    bool write_preset_file(const QString& path);
    void save_preset();
    void save_preset_as();
    void rename_preset();
    void new_rig();
    void open_preset_dialog();
    void populate_preset_menu();
    void step_preset(int step);
    QStringList preset_paths() const;
    // `path` becomes the current preset with the live graph as its baseline.
    void set_current_preset(const QString& path);
    void update_preset_state(); // the name and its `*`
    void toggle_favorite(bool on);
    static void seed_starter_presets();

    // Session: the graph and the current preset survive a restart.
    void save_session();
    void restore_session();
    // The chain-era session (QSettings session/…) as a v2 preset object.
    static QJsonObject legacy_session();

    // Search.
    void rebuild_search();
    void on_search_picked(const TopBar::SearchEntry& entry);

    unique_ptr<AudioEngine> engine;
    GraphModel* model = nullptr;
    LibraryIndex* library = nullptr;
    TopBar* top = nullptr;
    NodeCanvas* canvas = nullptr;
    ParamPanel* panel = nullptr;
    QSplitter* split = nullptr;
    AudioStatusBar* audio_bar = nullptr;
    QLabel* lbl_notice = nullptr;
    struct Notice
    {
        QString key, text, tip;
    };
    vector<Notice> notices;
    int non_finite_total = 0;
    bool audio_ok = false;

    // Settings window and what it drives.
    SettingsDialog* settings_dialog = nullptr;
    MidiInput* midi = nullptr;
    MidiMapper* midi_mapper = nullptr;
    bool midi_ok = false;
    QMenu* midi_menu = nullptr;
    QLabel* lbl_midi = nullptr; // status bar activity light
    QTimer* midi_blink = nullptr;
    QShortcut* learn_esc = nullptr;
    QWidget* learn_widget = nullptr; // glowing while learning
    QString learn_target;
    QFrame* tuner_strip = nullptr;
    TunerDisplay* tuner_display = nullptr;
    ToggleSwitch* tuner_mute = nullptr;
    NumberField* tuner_reference = nullptr; // A4; Settings → Tuner drives it too

    QTimer* meter_timer = nullptr;
    QTimer* status_timer = nullptr;
    QTimer* session_timer = nullptr;

    QString preset_path; // empty = untitled
    QJsonObject preset_baseline;
    bool restoring = false; // session restore in progress: no `*`, no save
};

} // namespace namx
