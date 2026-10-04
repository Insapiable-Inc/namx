#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QPoint>
#include <QTimer>

#include "AudioEngine.h"
#include "Common.h"
#include "EffectShortcuts.h"
#include "KnobWidget.h"
#include "LibraryIndex.h"
#include "ParamRegistry.h"
#include "PresetSerializer.h"
#include "SettingsDialog.h"
#include "ToggleSwitch.h"

class QDockWidget;
class QDragEnterEvent;
class QDropEvent;
class QMimeData;
class QFrame;
class QHBoxLayout;
class QVBoxLayout;
class QLabel;
class QPushButton;
class QToolButton;

class QMenu;
class QShortcut;

namespace namx
{

class AudioStatusBar;
class GainReductionMeter;
class LibraryPanel;
class PresetBar;
class MidiInput;
class MidiMapper;
class TempoBar;
class TunerDisplay;
class VUMeter;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Command-line / file-manager arguments: .nam, .wav, .nampreset (a preset
    // first, then a model/IR given alongside overrides the preset's own).
    void open_paths(const QStringList& paths);
    // Knobs and switches outside the tempo bar that presets would miss (not in the
    // registry); --self-test-and-exit fails on any.
    QStringList controls_missing_from_presets() const;

protected:
    // Drag-and-drop reordering of the effect strip (filter installed on the
    // effect cards and on the strip container).
    bool eventFilter(QObject* obj, QEvent* ev) override;
    // .nam / .wav / .nampreset files dropped anywhere on the window load.
    void dragEnterEvent(QDragEnterEvent* ev) override;
    void dropEvent(QDropEvent* ev) override;
    static bool has_loadable_urls(const QMimeData* mime);
    // Meters run at display rate only while the window can be seen.
    void changeEvent(QEvent* ev) override;
    void showEvent(QShowEvent* ev) override;
    void hideEvent(QHideEvent* ev) override;

private slots:
    // File dialogs — the one-off path; the Library is the primary one.
    void on_import_nam();
    void on_unload_nam();
    void on_import_ir();
    void on_unload_ir();

    void on_save_preset(); // overwrite the current file; Save As when untitled
    void on_save_preset_as();
    void on_load_preset();
    void on_import_presets(); // copy files into the presets folder, load the last
    void on_export_preset(); // write a copy elsewhere; the current file is unchanged
    void on_delete_preset();
    void on_locate_preset();
    void on_reset_all();

    void restart_audio_with_current_devices();

    void update_meters();
    void update_status(); // status bar metrics + disconnect watchdog + notices
    // The red notice zone in the middle of the status bar: one message per key
    // ("sampleRate", …) with an optional tooltip; an empty text removes it. Shown
    // joined, in posting order.
    void set_notice(const QString& key, const QString& text, const QString& tip = {});
    void update_exclusive_notice();
    void update_tuner();

private:
    void setup_ui();
    void setup_audio();
    void restore_last_files();
    void restore_connections();
    void save_connections();
    // Amplifier / Cabinet hero rows: file name + metadata line.
    void update_hero_cards();
    // Status bar, left: model / IR loaded at a glance.
    void update_rig_status();

    // Loading (every path in: dialogs, library, drops, session, presets)
    // Return false on failure; `quiet` suppresses the error dialog.
    bool load_model_file(const QString& path, bool quiet = false);
    bool load_ir_file(const QString& path, bool quiet = false);
    bool load_irb_file(const QString& path, bool quiet = false); // Cabinet slot B
    bool load_preset_file(const QString& path);
    // Registry values + chain order from a preset object (no files).
    PresetSerializer::Applied apply_preset_params(const QJsonObject& obj);
    void load_library_entry(LibraryEntry::Kind kind, const QString& path);
    // ‹ › on the hero rows: next/previous entry in the library's visible order.
    void step_library(LibraryEntry::Kind kind, int step);
    // Hero row scaffold: ‹ › Browse + name + metadata labels.
    void make_hero_controls(QWidget* card, LibraryEntry::Kind kind, QLabel*& name_out,
                            QLabel*& meta_out);

    // Row-card scaffold: [≡ grip | name label]. The caller appends its
    // controls to the card's QHBoxLayout.
    QFrame* make_row_card(const QString& title);
    // Full effect row: scaffold + enable toggle + stereo slot + knobs.
    QWidget* make_effect_row(const QString& title, ToggleSwitch*& toggle_out,
                             const std::initializer_list<KnobWidget*>& knobs,
                             ToggleSwitch** stereo_toggle_out = nullptr);

    // Tempo sync (Phase 15)
    // A time-based knob (delay time, LFO rate) gets a Sync toggle and a
    // note-value knob shown in its place while synced. All three are registry
    // parameters; each change re-pushes the effective value through `push`,
    // as does a tempo change.
    struct SyncedParam
    {
        KnobWidget* free_knob;
        KnobWidget* note_knob;
        ToggleSwitch* sync;
        bool is_time; // ms (delay) vs Hz (LFO)
        double min, max; // engine range; synced values are clamped
        std::function<void(double)> push;
    };
    void add_tempo_sync(QWidget* card, KnobWidget* free_knob, const QString& label, bool is_time,
                        int default_note, double min, double max, std::function<void(double)> push);
    SyncedParam& sync_of(KnobWidget* free_knob);
    void push_synced(KnobWidget* free_knob);
    void set_tempo(double new_bpm);

    // Second row of advanced controls under a card's main row, behind a
    // ▾ toggle at the row's right end. Starts at the knob column; expanded
    // state persists per card (QSettings ui/expandedCards).
    void add_detail_row(QWidget* card, const QList<QWidget*>& widgets);
    void set_tuner_reference(int hz);
    void set_tuner_show_hz(bool on);
    void set_transpose_continuous(bool on);
    QWidget* make_eq_freq_knob(const QString& key, const QString& label, double lo, double hi,
                               double def, EQProcessor::Band b, double off_at);
    QWidget* make_delay_pct_knob(const QString& key, const QString& label, double lo, double hi,
                                 double def, std::function<void(double)> f, const QString& tip);
    KnobWidget* make_ir_level_knob(int slot);
    QWidget* make_ir_pan_knob(int slot);
    QWidget* make_ir_phase_switch(int slot);
    QToolButton* make_ir_b_arrow(const QString& glyph, int step);
    void step_ir_b(int step);
    void choose_ir_b();
    void clear_ir_b();
    QWidget* make_gain_column(const QString& title, KnobWidget*& knob, VUMeter*& meter);
    void find_in_library();
    QToolButton* make_library_arrow(const QString& glyph, int step, const QString& tip,
                                    LibraryEntry::Kind kind);
    // MIDI action id of a hero ‹ › arrow ("action:modelNext", …).
    static QString library_step_action(LibraryEntry::Kind kind, int step);
    void show_library_section(LibraryEntry::Kind kind);
    void show_midi_light(bool lit);
    void on_midi_activity(const QString& what);
    void open_midi_mappings();
    // Shows the settings window (created on first use) on `page`.
    void open_settings(SettingsDialog::Page page);
    void populate_midi_devices_menu(QMenu* devices);
    void show_file_status(QLabel* name, QLabel* meta, bool loaded, const QString& path,
                          const QString& empty_text);
    bool try_load_referenced_file(const QString& path, LibraryEntry::Kind kind);

    // Effect strip / chain reordering
    // Lays the cards out in the given order: dividers follow NAM/EQ.
    void layout_strip(const vector<EffectBase*>& order);
    // layoutStrip(engine chain) + shortcuts re-assigned left to right.
    void rebuild_effect_strip();
    // Keep bypassed cards visually subordinate without disabling their controls.
    void update_card_bypass_state(EffectBase* effect, bool enabled);
    // setChainOrder + strip rebuild; used by session restore, preset load, DnD.
    void apply_chain_order(const QStringList& ids);
    // Live drag preview: while a card is dragged the strip re-lays out to the
    // prospective order (engine untouched); drop commits it, cancel restores.
    void start_card_drag(QWidget* card);
    void update_drag_preview(const QPoint& pos);
    void commit_drag_preview();
    void cancel_drag_preview();
    // Card context menu: reset one effect's sound parameters (knobs + stereo)
    // to defaults; the enable toggle keeps its state.
    void reset_card_to_defaults(QWidget* card);

    // Parameter registry (ParamDesc lives in ParamRegistry.h)
    // Registration wires each widget to the engine; the same table then
    // drives preset save/load (PresetSerializer) and session persistence.
    void register_parameters();
    void knob_param(const QString& group, const QString& key, KnobWidget* knob,
                    const std::function<void(double)>& apply);
    void toggle_param(const QString& group, const QString& key, ToggleSwitch* sw,
                      const std::function<void(bool)>& apply);

    // Current preset: name, `*` state, A/B
    // Serialized state written by Save (registry + order + model/IR).
    QJsonObject current_preset_json() const;
    bool write_preset_file(const QString& path);
    // Makes `path` the current preset with the live state as its baseline
    // (empty = untitled); resets A/B.
    void set_current_preset(const QString& path);
    // Recomputes the `*`: live state vs the baseline. Coalesced via a timer.
    void update_preset_state();
    void step_preset(int step);
    void switch_ab(int side);
    void copy_ab();
    void restore_preset_state();
    static void seed_starter_presets();

    // MIDI (Phase 16)
    // Every registry knob/toggle is a target (registered by knobParam /
    // toggleParam), plus the actions added here.
    void setup_midi(QMenu* top_menu);
    QString target_label_for(QWidget* w, const QString& group, const QString& key) const;
    void add_learn_entries(const QString& target_id, QMenu* menu);
    void start_learn(const QString& target_id);
    void on_learn_changed();
    QWidget* widget_for_target(const QString& target_id) const;

    // Session persistence: same table, QSettings instead of a preset file.
    void save_session_state();
    void restore_session_state();
    void schedule_session_save();

    ParamRegistry params;
    // Parameters created in setupUI that don't need a named member (the
    // Phase 17 advanced knobs): registered at the end of registerParameters.
    vector<std::function<void()>> late_params;
    KnobWidget* late_knob(const QString& group, const QString& key, KnobWidget* k,
                          std::function<void(double)> apply);

    unique_ptr<AudioEngine> engine;

    // Effect strip state
    std::unordered_map<EffectBase*, QWidget*> fx_cols;
    std::unordered_map<EffectBase*, ToggleSwitch*> fx_toggles; // each effect's on/off switch
    std::unordered_map<QWidget*, QWidget*> card_grips; // card → ≡ drag handle
    QWidget* strip = nullptr;
    QVBoxLayout* strip_lay = nullptr;
    QWidget* input_card = nullptr;
    QWidget* output_card = nullptr;
    EffectShortcuts shortcuts{this};

    // Drag state for card reordering
    QWidget* drag_card = nullptr;
    QPoint drag_start_pos;
    string drag_id; // effect ID being dragged; empty = no drag
    vector<string> preview_ids; // strip order currently shown while dragging

    QLabel* lbl_nam_status = nullptr; // file name (accent when loaded)
    QLabel* lbl_nam_meta = nullptr; // architecture · rate · gear
    QLabel* lbl_ir_status = nullptr;
    QLabel* lbl_ir_meta = nullptr;
    QLabel* lbl_irb_status = nullptr; // Cabinet slot B (detail row)

    LibraryIndex* library_index = nullptr;
    QHash<QString, std::pair<qint64, LibraryEntry>> described; // see describe_cached
    LibraryEntry describe_cached(const QString& path);
    LibraryPanel* library = nullptr; // the Library dock's content
    QDockWidget* library_dock = nullptr;

    AudioStatusBar* audio_bar = nullptr;
    QLabel* lbl_rig_status = nullptr;
    // Last known backend state (drives the disconnect watchdog in update_meters).
    bool audio_ok = false;

    VUMeter* meter_in = nullptr;
    VUMeter* meter_out = nullptr;
    GainReductionMeter* gr_gate = nullptr;
    GainReductionMeter* gr_comp = nullptr;

    TunerDisplay* tuner_display = nullptr;
    NumberField* tuner_reference_box = nullptr; // on the tuner card; the settings page drives it
    SettingsDialog* settings_dialog = nullptr;
    int tuner_ref_hz = 440;

    KnobWidget* knob_input_gain = nullptr;
    KnobWidget* knob_output_gain = nullptr;

    ToggleSwitch* sw_eq_enable = nullptr;
    KnobWidget* knob_low = nullptr;
    KnobWidget* knob_mid = nullptr;
    KnobWidget* knob_treble = nullptr;

    ToggleSwitch* sw_tuner_enable = nullptr;
    ToggleSwitch* sw_tuner_mute = nullptr;

    ToggleSwitch* sw_gate_enable = nullptr;
    KnobWidget* knob_gate_thresh = nullptr;
    KnobWidget* knob_gate_attack = nullptr;
    KnobWidget* knob_gate_release = nullptr;

    ToggleSwitch* sw_comp_enable = nullptr;
    KnobWidget* knob_comp_thresh = nullptr;
    KnobWidget* knob_comp_ratio = nullptr;
    KnobWidget* knob_comp_attack = nullptr;
    KnobWidget* knob_comp_release = nullptr;
    KnobWidget* knob_comp_makeup = nullptr;

    ToggleSwitch* sw_wah_enable = nullptr;
    KnobWidget* knob_wah_sens = nullptr;
    KnobWidget* knob_wah_res = nullptr;
    KnobWidget* knob_wah_mix = nullptr;

    ToggleSwitch* sw_flanger_enable = nullptr;
    ToggleSwitch* sw_flanger_stereo = nullptr;
    KnobWidget* knob_fl_rate = nullptr;
    KnobWidget* knob_fl_depth = nullptr;
    KnobWidget* knob_fl_feedback = nullptr;
    KnobWidget* knob_fl_mix = nullptr;

    ToggleSwitch* sw_phaser_enable = nullptr;
    ToggleSwitch* sw_phaser_stereo = nullptr;
    ToggleSwitch* sw_wah_manual = nullptr; // Mode: Auto / Manual
    ToggleSwitch* sw_wah_reverse = nullptr;
    KnobWidget* knob_ph_rate = nullptr;
    KnobWidget* knob_ph_depth = nullptr;
    KnobWidget* knob_ph_feedback = nullptr;
    KnobWidget* knob_ph_mix = nullptr;

    ToggleSwitch* sw_chorus_enable = nullptr;
    ToggleSwitch* sw_chorus_stereo = nullptr;
    KnobWidget* knob_ch_rate = nullptr;
    KnobWidget* knob_ch_depth = nullptr;
    KnobWidget* knob_ch_mix = nullptr;

    ToggleSwitch* sw_tremolo_enable = nullptr;
    ToggleSwitch* sw_tremolo_stereo = nullptr;
    KnobWidget* knob_tr_rate = nullptr;
    KnobWidget* knob_tr_depth = nullptr;

    ToggleSwitch* sw_delay_enable = nullptr;
    ToggleSwitch* sw_delay_stereo = nullptr;
    KnobWidget* knob_del_time = nullptr;
    KnobWidget* knob_del_feedback = nullptr;
    KnobWidget* knob_del_mix = nullptr;

    ToggleSwitch* sw_reverb_enable = nullptr;
    KnobWidget* knob_rev_room = nullptr;
    KnobWidget* knob_rev_damping = nullptr;
    KnobWidget* knob_rev_width = nullptr;
    KnobWidget* knob_rev_mix = nullptr;
    KnobWidget* knob_rev_pre = nullptr;

    ToggleSwitch* sw_transpose_enable = nullptr;
    ToggleSwitch* sw_transpose_reverse = nullptr;
    ToggleSwitch* sw_transpose_continuous = nullptr;
    KnobWidget* knob_transpose_semis = nullptr;
    ToggleSwitch* sw_drive_enable = nullptr;
    KnobWidget* knob_nam_input = nullptr;
    KnobWidget* knob_nam_output = nullptr;
    KnobWidget* knob_drive = nullptr;
    KnobWidget* knob_drive_tone = nullptr;
    KnobWidget* knob_drive_level = nullptr;

    ToggleSwitch* sw_volume_enable = nullptr;
    ToggleSwitch* sw_volume_reverse = nullptr;
    KnobWidget* knob_vol_pos = nullptr;
    KnobWidget* knob_vol_mid = nullptr;

    ToggleSwitch* sw_limiter = nullptr; // Output card

    ToggleSwitch* sw_nam_enable = nullptr;
    ToggleSwitch* sw_ir_enable = nullptr;

    void update_meter_timer();

    QTimer* meter_timer = nullptr; // ~60 fps, paused while minimized/hidden
    QTimer* status_timer = nullptr; // 50 ms, always
    QTimer* tuner_timer = nullptr;
    QTimer* session_save_timer = nullptr;

    TempoBar* tempo_bar = nullptr;
    double bpm = 120.0;
    vector<SyncedParam> synced;
    PresetBar* preset_bar = nullptr;
    QString preset_path; // current preset file; empty = untitled
    QJsonObject preset_baseline; // toJson() when it was loaded/saved
    QHash<QString, QVariant> pending_settings; // see persist_setting
    void persist_setting(const QString& key, const QVariant& value);
    QString shown_preset_path; // what the preset bar last showed (update_preset_state)
    bool shown_preset_modified = false;
    QJsonObject ab[2]; // A/B snapshots; empty = not taken yet
    int ab_active = 0;
    QTimer* preset_state_timer = nullptr;

    MidiInput* midi = nullptr; // child of this window
    MidiMapper* midi_mapper = nullptr;
    bool midi_ok = false;
    QShortcut* learn_esc = nullptr;
    QWidget* learn_widget = nullptr; // highlighted while learning
    QHash<QString, QWidget*> library_arrows; // hero ‹ › by MIDI action id
    QString learn_target;
    QLabel* lbl_midi = nullptr; // status bar activity light
    QLabel* lbl_notice = nullptr; // status bar middle: problems worth seeing (set_notice)
    struct Notice
    {
        QString key, text, tip;
    };
    vector<Notice> notices;
    int non_finite_total =
        0; // blocks scrubbed since launch (status-bar notice) // in posting order
    QMenu* midi_menu = nullptr;
    QTimer* midi_blink = nullptr;
};

} // namespace namx
