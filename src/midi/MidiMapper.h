#pragma once

#include <functional>
#include <vector>

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include "Common.h"
#include "MidiMessage.h"

class QWidget;

namespace npl
{

class KnobWidget;
class ToggleSwitch;

// One MIDI → target binding.
struct MidiMapping
{
    enum Source
    {
        CC,
        NOTE,
        PROGRAM
    };
    // Absolute: CC value / note on-off sets the target. Relative: CC is a
    // signed increment (1…63 up, 127…65 down — endless encoders). Toggle:
    // each press (CC rising past 64, note on, program) flips a switch or
    // fires an action. Momentary: on while held.
    enum Mode
    {
        ABSOLUTE,
        RELATIVE,
        TOGGLE,
        MOMENTARY
    };

    bool enabled = true;
    Source source = CC;
    int channel = -1; // -1 = any channel
    int number = 0; // CC#, note, program
    QString target; // "param:<group>/<key>" or "action:<name>"
    Mode mode = ABSOLUTE;
    double min = 0.0, max = 1.0; // knob range share the controller sweeps (0…1)

    QJsonObject to_json() const;
    static MidiMapping from_json(const QJsonObject& o);
    bool matches(const MidiMessage& m) const;
};

// Something MIDI can drive: a registry knob or toggle (applied through the
// widget, so the engine, session and preset `*` follow exactly as for a
// mouse change) or an action (slot recall, next preset, tap tempo…).
struct MidiTarget
{
    QString id, label, group;
    KnobWidget* knob = nullptr;
    ToggleSwitch* toggle = nullptr;
    std::function<void()> action;
};

// Phase 16 mapping engine. Lives on the GUI thread; MidiInput's queued
// signal feeds handle(). Mappings persist in QSettings midi/mappings and
// as .nammidi files (Mappings window → Save / Load).
class MidiMapper : public QObject
{
    Q_OBJECT
public:
    explicit MidiMapper(QObject* parent = nullptr);

    void add_knob(const QString& id, const QString& group, const QString& label, KnobWidget* k);
    void add_toggle(const QString& id, const QString& group, const QString& label, ToggleSwitch* t);
    void add_action(const QString& id, const QString& group, const QString& label,
                    std::function<void()> fn);
    const vector<MidiTarget>& get_targets() const {
        return targets;
    }
    const MidiTarget* target(const QString& id) const;
    // Target driven by this widget (knob or toggle), or null.
    const MidiTarget* target_for(const QWidget* w) const;

    const QList<MidiMapping>& get_mappings() const {
        return mappings;
    }
    void set_mappings(const QList<MidiMapping>& m); // persists, emits mappingsChanged
    static QList<MidiMapping> default_mappings(); // program 0–7 → slots 1–8
    bool has_mapping(const QString& target_id) const;
    QString mapping_summary(const QString& target_id) const; // "CC 11 ch 1" for menus
    void forget(const QString& target_id);

    bool save_file(const QString& path) const;
    bool load_file(const QString& path, QString* error = nullptr);

    // Learn: the next CC / note-on / program change binds to `targetId`
    // (replacing that target's mappings). learnRow binds into an existing
    // mapping row instead (Mappings window).
    void learn(const QString& target_id);
    void learn_row(qsizetype row);
    void cancel_learn();
    QString learning() const {
        return learn_target;
    }
    qsizetype learning_row() const {
        return pending_learn_row;
    }

    bool get_follow_clock() const {
        return follow_clock;
    }
    void set_follow_clock(bool on);

public slots:
    void handle(const MidiMessage& m);

signals:
    void mappings_changed();
    void learn_changed(); // learning() / learningRow() changed
    void activity(const QString& what); // last message, for the UI
    void tempo_from_clock(double bpm);

private:
    void apply(MidiMapping& mp, int index, const MidiMessage& m);
    void handle_clock(const MidiMessage& m);
    void save() const;

    vector<MidiTarget> targets;
    QList<MidiMapping> mappings;
    QHash<int, int> last_value; // mapping index → last CC value (edge detection)
    QString learn_target;
    qsizetype pending_learn_row = -1;

    bool follow_clock = false;
    QElapsedTimer clock;
    qint64 beat_start_ms = -1;
    int clock_ticks = 0;
    QList<double> beat_ms; // recent beat lengths (24 ticks each)
    double last_clock_bpm = 0.0;
};

} // namespace npl
