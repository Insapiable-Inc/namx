#pragma once

#include <functional>
#include <vector>

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include "MidiMessage.h"

class KnobWidget;
class ToggleSwitch;
class QWidget;

/// One MIDI → target binding.
struct MidiMapping
{
    enum Source
    {
        CC,
        Note,
        Program
    };
    /// Absolute: CC value / note on-off sets the target. Relative: CC is a
    /// signed increment (1…63 up, 127…65 down — endless encoders). Toggle:
    /// each press (CC rising past 64, note on, program) flips a switch or
    /// fires an action. Momentary: on while held.
    enum Mode
    {
        Absolute,
        Relative,
        Toggle,
        Momentary
    };

    bool enabled = true;
    Source source = CC;
    int channel = -1; // -1 = any channel
    int number = 0; // CC#, note, program
    QString target; // "param:<group>/<key>" or "action:<name>"
    Mode mode = Absolute;
    double min = 0.0, max = 1.0; // knob range share the controller sweeps (0…1)

    QJsonObject toJson() const;
    static MidiMapping fromJson(const QJsonObject& o);
    bool matches(const MidiMessage& m) const;
};

/// Something MIDI can drive: a registry knob or toggle (applied through the
/// widget, so the engine, session and preset `*` follow exactly as for a
/// mouse change) or an action (slot recall, next preset, tap tempo…).
struct MidiTarget
{
    QString id, label, group;
    KnobWidget* knob = nullptr;
    ToggleSwitch* toggle = nullptr;
    std::function<void()> action;
};

/// Phase 16 mapping engine. Lives on the GUI thread; MidiInput's queued
/// signal feeds handle(). Mappings persist in QSettings midi/mappings and
/// as .nammidi files (Mappings window → Save / Load).
class MidiMapper : public QObject
{
    Q_OBJECT
public:
    explicit MidiMapper(QObject* parent = nullptr);

    void addKnob(const QString& id, const QString& group, const QString& label, KnobWidget* k);
    void addToggle(const QString& id, const QString& group, const QString& label, ToggleSwitch* t);
    void addAction(const QString& id, const QString& group, const QString& label,
                   std::function<void()> fn);
    const std::vector<MidiTarget>& targets() const {
        return mTargets;
    }
    const MidiTarget* target(const QString& id) const;
    /// Target driven by this widget (knob or toggle), or null.
    const MidiTarget* targetFor(const QWidget* w) const;

    const QList<MidiMapping>& mappings() const {
        return mMappings;
    }
    void setMappings(const QList<MidiMapping>& m); // persists, emits mappingsChanged
    static QList<MidiMapping> defaultMappings(); // program 0–7 → slots 1–8
    bool hasMapping(const QString& targetId) const;
    QString mappingSummary(const QString& targetId) const; // "CC 11 ch 1" for menus
    void forget(const QString& targetId);

    bool saveFile(const QString& path) const;
    bool loadFile(const QString& path, QString* error = nullptr);

    /// Learn: the next CC / note-on / program change binds to `targetId`
    /// (replacing that target's mappings). learnRow binds into an existing
    /// mapping row instead (Mappings window).
    void learn(const QString& targetId);
    void learnRow(int row);
    void cancelLearn();
    QString learning() const {
        return mLearnTarget;
    }
    int learningRow() const {
        return mLearnRow;
    }

    bool followClock() const {
        return mFollowClock;
    }
    void setFollowClock(bool on);

public slots:
    void handle(const MidiMessage& m);

signals:
    void mappingsChanged();
    void learnChanged(); // learning() / learningRow() changed
    void activity(const QString& what); // last message, for the UI
    void tempoFromClock(double bpm);

private:
    void apply(MidiMapping& mp, int index, const MidiMessage& m);
    void handleClock(const MidiMessage& m);
    void save() const;

    std::vector<MidiTarget> mTargets;
    QList<MidiMapping> mMappings;
    QHash<int, int> mLastValue; // mapping index → last CC value (edge detection)
    QString mLearnTarget;
    int mLearnRow = -1;

    bool mFollowClock = false;
    QElapsedTimer mClock;
    qint64 mBeatStartMs = -1;
    int mClockTicks = 0;
    QList<double> mBeatMs; // recent beat lengths (24 ticks each)
    double mLastClockBpm = 0.0;
};
