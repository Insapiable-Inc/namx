// Headless test of MidiMapper (Phase 16): learn, CC absolute / relative,
// footswitch toggle edges, momentary, program change → action, note
// toggles, channel filtering, default mappings, file round trip, and MIDI
// clock → tempo. Messages are fed straight into handle(), so no MIDI
// hardware or sequencer is needed; the platform input is a thin parser.
//
// Runs with a temporary QSettings scope — never touches the user's config.

#include "MidiMapper.h"

#include <cmath>
#include <cstdio>

#include <QApplication>
#include <QElapsedTimer>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>

#include "KnobWidget.h"
#include "ToggleSwitch.h"

static int gFailures = 0;
#define CHECK(cond, msg) \
    do { \
        if (cond) \
            std::printf("  ok   %s\n", msg); \
        else { \
            std::printf("  FAIL %s  (%s:%d)\n", msg, __FILE__, __LINE__); \
            ++gFailures; \
        } \
    } while (0)

static MidiMessage cc(int ch, int num, int val) {
    return {MidiMessage::ControlChange, ch, num, val};
}
static MidiMessage noteOn(int ch, int num) {
    return {MidiMessage::NoteOn, ch, num, 100};
}
static MidiMessage noteOff(int ch, int num) {
    return {MidiMessage::NoteOff, ch, num, 0};
}
static MidiMessage program(int ch, int num) {
    return {MidiMessage::ProgramChange, ch, num, 0};
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv); // widgets (knob/toggle targets)
    app.setOrganizationName("pichulero.cl");
    app.setApplicationName("NAM Player Test");
    QTemporaryDir tmp;
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, tmp.path() + "/cfg");
    QSettings().clear();

    MidiMapper mapper;
    KnobWidget mix("Mix", 0.0, 100.0, 30.0, "%");
    ToggleSwitch delayOn;
    delayOn.setCheckable(true);
    int slotFired[8] = {};
    mapper.addKnob("param:delay/mix", "Delay", "Mix", &mix);
    mapper.addToggle("param:delay/enabled", "Delay", "On / Off", &delayOn);
    for (int i = 0; i < 8; ++i) {
        mapper.addAction(QString("action:slot%1").arg(i + 1), "Presets", "Slot", [&slotFired, i] {
            ++slotFired[i];
        });
    }

    std::printf("defaults\n");
    CHECK(mapper.mappings().size() == 8, "first run: 8 default mappings");
    mapper.handle(program(3, 2));
    CHECK(slotFired[2] == 1, "program 2 (any channel) recalls slot 3");

    std::printf("learn\n");
    mapper.learn("param:delay/mix");
    mapper.handle(cc(0, 11, 0)); // a release / zero doesn't bind
    CHECK(mapper.learning() == "param:delay/mix", "CC value 0 doesn't complete learn");
    mapper.handle(cc(0, 11, 64));
    CHECK(mapper.learning().isEmpty(), "CC 11 completes learn");
    CHECK(mapper.hasMapping("param:delay/mix"), "mapping created");
    CHECK(mapper.mappingSummary("param:delay/mix") == "CC 11 ch 1", "summary text");

    std::printf("absolute CC\n");
    mapper.handle(cc(0, 11, 127));
    CHECK(std::abs(mix.value() - 100.0) < 0.05, "CC 127 → max");
    mapper.handle(cc(0, 11, 0));
    CHECK(std::abs(mix.value()) < 0.05, "CC 0 → min");
    mapper.handle(cc(1, 11, 127));
    CHECK(std::abs(mix.value()) < 0.05, "other channel ignored (learned channel 1)");

    std::printf("range + relative\n");
    {
        auto list = mapper.mappings();
        list.last().min = 0.2;
        list.last().max = 0.6;
        mapper.setMappings(list);
        mapper.handle(cc(0, 11, 127));
        CHECK(std::abs(mix.value() - 60.0) < 0.05, "max 60 % of the range");
        list.last().mode = MidiMapping::Relative;
        mapper.setMappings(list);
        mix.setValue(40.0);
        mapper.handle(cc(0, 11, 1));
        mapper.handle(cc(0, 11, 1));
        CHECK(mix.value() > 40.0, "relative +1 twice moves up");
        const double up = mix.value();
        mapper.handle(cc(0, 11, 127)); // −1
        CHECK(mix.value() < up, "relative 127 = −1 moves down");
    }

    std::printf("footswitch toggle\n");
    mapper.learn("param:delay/enabled");
    mapper.handle(cc(0, 80, 127));
    CHECK(mapper.hasMapping("param:delay/enabled"), "switch learned");
    CHECK(!delayOn.isChecked(), "learning press doesn't also toggle");
    mapper.handle(cc(0, 80, 127)); // press
    CHECK(delayOn.isChecked(), "press → on");
    mapper.handle(cc(0, 80, 127)); // held/repeated without release: no flip
    CHECK(delayOn.isChecked(), "no flip without a release in between");
    mapper.handle(cc(0, 80, 0)); // release
    mapper.handle(cc(0, 80, 127)); // press
    CHECK(!delayOn.isChecked(), "second press → off");

    std::printf("momentary + notes\n");
    {
        auto list = mapper.mappings();
        for (auto& m : list) {
            if (m.target == "param:delay/enabled") {
                m.mode = MidiMapping::Momentary;
                m.source = MidiMapping::Note;
                m.number = 60;
            }
        }
        mapper.setMappings(list);
        mapper.handle(noteOn(0, 60));
        CHECK(delayOn.isChecked(), "note on → on (momentary)");
        mapper.handle(noteOff(0, 60));
        CHECK(!delayOn.isChecked(), "note off → off (momentary)");
    }

    std::printf("disabled mapping + forget\n");
    {
        auto list = mapper.mappings();
        for (auto& m : list) {
            if (m.target == "action:slot1") {
                m.enabled = false;
            }
        }
        mapper.setMappings(list);
        mapper.handle(program(0, 0));
        CHECK(slotFired[0] == 0, "disabled mapping does nothing");
        mapper.forget("param:delay/mix");
        CHECK(!mapper.hasMapping("param:delay/mix"), "forget removes the mapping");
    }

    std::printf("file round trip + persistence\n");
    {
        const QString path = tmp.path() + "/test.nammidi";
        CHECK(mapper.saveFile(path), "save .nammidi");
        const int n = mapper.mappings().size();
        mapper.setMappings(MidiMapper::defaultMappings());
        QString err;
        CHECK(mapper.loadFile(path, &err), "load .nammidi");
        CHECK(mapper.mappings().size() == n, "same number of mappings after load");
        MidiMapper second; // reads QSettings midi/mappings
        CHECK(second.mappings().size() == n, "mappings persist in QSettings");
    }

    std::printf("MIDI clock → tempo\n");
    {
        double bpm = 0.0;
        QObject::connect(&mapper, &MidiMapper::tempoFromClock, [&bpm](double b) {
            bpm = b;
        });
        mapper.setFollowClock(true);
        // 125 BPM: a quarter = 480 ms, a clock tick = 20 ms.
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < 24 * 3 + 1; ++i) {
            while (t.elapsed() < i * 20) {
                QThread::usleep(200);
            }
            mapper.handle({MidiMessage::Clock, 0, 0, 0});
        }
        CHECK(std::abs(bpm - 125.0) < 1.5, qPrintable(QString("clock at 125 BPM → %1").arg(bpm)));
    }

    std::printf(gFailures ? "\n%d FAILED\n" : "\nall passed\n", gFailures);
    return gFailures ? 1 : 0;
}
