// Headless test of MidiMapper (Phase 16): learn, CC absolute / relative,
// footswitch toggle edges, momentary, program change → action, note
// toggles, channel filtering, one control → many targets (multi-assign),
// default mappings, file round trip, and MIDI clock → tempo. Messages are
// fed straight into handle(), so no MIDI hardware or sequencer is needed; the
// platform input is a thin parser.
//
// Runs with a temporary QSettings scope — never touches the user's config.

#include "MidiMapper.h"

#include <cmath>
#include <cstdio>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>

#include "Common.h"
#include "KnobWidget.h"
#include "ToggleSwitch.h"

namespace namx
{

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
    return {MidiMessage::CONTROL_CHANGE, ch, num, val};
}
static MidiMessage note_on(int ch, int num) {
    return {MidiMessage::NOTE_ON, ch, num, 100};
}
static MidiMessage note_off(int ch, int num) {
    return {MidiMessage::NOTE_OFF, ch, num, 0};
}
static MidiMessage program(int ch, int num) {
    return {MidiMessage::PROGRAM_CHANGE, ch, num, 0};
}

int run(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv); // widgets (knob/toggle targets)
    app.setOrganizationName("pichulero.cl");
    app.setApplicationName("NAM-X Test");
    QTemporaryDir tmp;
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, tmp.path() + "/cfg");
    QSettings().clear();

    MidiMapper mapper;
    KnobWidget mix("Mix", 0.0, 100.0, 30.0, "%");
    ToggleSwitch delay_on;
    delay_on.setCheckable(true);
    int slot_fired[8] = {};
    mapper.add_knob("param:delay/mix", "Delay", "Mix", &mix);
    mapper.add_toggle("param:delay/enabled", "Delay", "On / Off", &delay_on);
    for (int i = 0; i < 8; ++i) {
        mapper.add_action(QString("action:slot%1").arg(i + 1), "Presets", "Slot", [&slot_fired, i] {
            ++slot_fired[i];
        });
    }

    std::printf("defaults\n");
    CHECK(mapper.get_mappings().size() == 8, "first run: 8 default mappings");
    mapper.handle(program(3, 2));
    CHECK(slot_fired[2] == 1, "program 2 (any channel) recalls slot 3");

    std::printf("learn\n");
    mapper.learn("param:delay/mix");
    mapper.handle(cc(0, 11, 0)); // a release / zero doesn't bind
    CHECK(mapper.learning() == "param:delay/mix", "CC value 0 doesn't complete learn");
    mapper.handle(cc(0, 11, 64));
    CHECK(mapper.learning().isEmpty(), "CC 11 completes learn");
    CHECK(mapper.has_mapping("param:delay/mix"), "mapping created");
    CHECK(mapper.mapping_summary("param:delay/mix") == "CC 11 ch 1", "summary text");

    std::printf("absolute CC\n");
    mapper.handle(cc(0, 11, 127));
    CHECK(std::abs(mix.value() - 100.0) < 0.05, "CC 127 → max");
    mapper.handle(cc(0, 11, 0));
    CHECK(std::abs(mix.value()) < 0.05, "CC 0 → min");
    mapper.handle(cc(1, 11, 127));
    CHECK(std::abs(mix.value()) < 0.05, "other channel ignored (learned channel 1)");

    std::printf("range + relative\n");
    {
        QList<MidiMapping> list = mapper.get_mappings();
        list.last().min = 0.2;
        list.last().max = 0.6;
        mapper.set_mappings(list);
        mapper.handle(cc(0, 11, 127));
        CHECK(std::abs(mix.value() - 60.0) < 0.05, "max 60 % of the range");
        list.last().mode = MidiMapping::RELATIVE;
        mapper.set_mappings(list);
        mix.set_value(40.0);
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
    CHECK(mapper.has_mapping("param:delay/enabled"), "switch learned");
    CHECK(!delay_on.isChecked(), "learning press doesn't also toggle");
    mapper.handle(cc(0, 80, 127)); // press
    CHECK(delay_on.isChecked(), "press → on");
    mapper.handle(cc(0, 80, 127)); // held/repeated without release: no flip
    CHECK(delay_on.isChecked(), "no flip without a release in between");
    mapper.handle(cc(0, 80, 0)); // release
    mapper.handle(cc(0, 80, 127)); // press
    CHECK(!delay_on.isChecked(), "second press → off");

    std::printf("momentary + notes\n");
    {
        QList<MidiMapping> list = mapper.get_mappings();
        for (auto& m : list) {
            if (m.target == "param:delay/enabled") {
                m.mode = MidiMapping::MOMENTARY;
                m.source = MidiMapping::NOTE;
                m.number = 60;
            }
        }
        mapper.set_mappings(list);
        mapper.handle(note_on(0, 60));
        CHECK(delay_on.isChecked(), "note on → on (momentary)");
        mapper.handle(note_off(0, 60));
        CHECK(!delay_on.isChecked(), "note off → off (momentary)");
    }

    std::printf("disabled mapping + forget\n");
    {
        QList<MidiMapping> list = mapper.get_mappings();
        for (auto& m : list) {
            if (m.target == "action:slot1") {
                m.enabled = false;
            }
        }
        mapper.set_mappings(list);
        mapper.handle(program(0, 0));
        CHECK(slot_fired[0] == 0, "disabled mapping does nothing");
        mapper.forget("param:delay/mix");
        CHECK(!mapper.has_mapping("param:delay/mix"), "forget removes the mapping");
    }

    // One control, many targets (a user-facing feature): learn replaces only the
    // learned target's own binding, so a CC learned on a second control drives both.
    // Min > Max inverts a sweep: one pedal crossfades a volume up and a wah down.
    std::printf("multi-assign\n");
    {
        KnobWidget volume("Volume", 0.0, 100.0, 0.0, "%");
        KnobWidget wah("Position", 0.0, 100.0, 0.0, "%");
        ToggleSwitch chorus_on;
        chorus_on.setCheckable(true);
        ToggleSwitch reverb_on;
        reverb_on.setCheckable(true);
        reverb_on.setChecked(true);
        mapper.add_knob("param:volume/volume", "Volume", "Volume", &volume);
        mapper.add_knob("param:dynamicWah/position", "Wah", "Position", &wah);
        mapper.add_toggle("param:chorus/enabled", "Chorus", "On / Off", &chorus_on);
        mapper.add_toggle("param:reverb/enabled", "Reverb", "On / Off", &reverb_on);

        mapper.learn("param:volume/volume");
        mapper.handle(cc(0, 7, 64));
        mapper.learn("param:dynamicWah/position");
        mapper.handle(cc(0, 7, 64));
        CHECK(mapper.has_mapping("param:volume/volume") &&
                  mapper.has_mapping("param:dynamicWah/position"),
              "learning CC 7 on a second knob keeps the first");
        QList<MidiMapping> list = mapper.get_mappings();
        for (auto& m : list) {
            if (m.target == "param:dynamicWah/position") {
                m.min = 1.0;
                m.max = 0.0;
            }
        }
        mapper.set_mappings(list);
        mapper.handle(cc(0, 7, 127));
        CHECK(std::abs(volume.value() - 100.0) < 0.05 && std::abs(wah.value()) < 0.05,
              "pedal down: volume max, inverted wah min");
        mapper.handle(cc(0, 7, 0));
        CHECK(std::abs(volume.value()) < 0.05 && std::abs(wah.value() - 100.0) < 0.05,
              "pedal up: volume min, inverted wah max");

        // Toggle on a knob the dial quantizes (log taper, a Max off its steps): the
        // second press must come back down (audit 2026-10-04, Fable item 05).
        KnobWidget cut("Low cut", 20.0, 2000.0, 20.0, "Hz");
        cut.set_log_taper(true);
        mapper.add_knob("param:eq/lowCut", "EQ", "Low cut", &cut);
        mapper.learn("param:eq/lowCut");
        mapper.handle(cc(0, 81, 127));
        mapper.handle(cc(0, 81, 0));
        list = mapper.get_mappings();
        for (auto& m : list) {
            if (m.target == "param:eq/lowCut") {
                m.mode = MidiMapping::TOGGLE;
                m.min = 0.0;
                m.max = 0.25; // 515 Hz, between two dial steps on the log taper
            }
        }
        mapper.set_mappings(list);
        mapper.handle(cc(0, 81, 127));
        mapper.handle(cc(0, 81, 0));
        CHECK(std::abs(cut.value() - 515.0) < 0.5, "toggle press: knob at Max");
        mapper.handle(cc(0, 81, 127));
        mapper.handle(cc(0, 81, 0));
        CHECK(std::abs(cut.value() - 20.0) < 0.5, "second press: back at Min");

        // The card's Reverse switch swaps heel and toe on top of the mapping range.
        ToggleSwitch volume_reverse;
        volume_reverse.setCheckable(true);
        mapper.set_reverse_switch("param:volume/volume", &volume_reverse);
        volume_reverse.setChecked(true);
        mapper.handle(cc(0, 7, 127));
        CHECK(std::abs(volume.value()) < 0.05 && std::abs(wah.value()) < 0.05,
              "Reverse: pedal down → volume min; the wah (no Reverse) unchanged");
        volume_reverse.setChecked(false);
        mapper.set_reverse_switch("param:volume/volume", nullptr);

        mapper.learn("param:chorus/enabled");
        mapper.handle(cc(0, 81, 127));
        mapper.handle(cc(0, 81, 0));
        mapper.learn("param:reverb/enabled");
        mapper.handle(cc(0, 81, 127));
        mapper.handle(cc(0, 81, 0));
        mapper.handle(cc(0, 81, 127));
        CHECK(chorus_on.isChecked() && !reverb_on.isChecked(),
              "one footswitch alternates chorus (was off) and reverb (was on)");

        mapper.learn("param:volume/volume");
        mapper.handle(cc(0, 8, 64));
        CHECK(mapper.mapping_summary("param:volume/volume") == "CC 8 ch 1" &&
                  mapper.has_mapping("param:dynamicWah/position"),
              "re-learning a target replaces only its own binding");
        for (const char* id : {"param:volume/volume", "param:dynamicWah/position",
                               "param:chorus/enabled", "param:reverb/enabled"}) {
            mapper.forget(id); // the widgets go out of scope
        }
    }

    std::printf("file round trip + persistence\n");
    {
        const QString path = tmp.path() + "/test.nammidi";
        CHECK(mapper.save_file(path), "save .nammidi");
        const qsizetype n = mapper.get_mappings().size();
        mapper.set_mappings(MidiMapper::default_mappings());
        QString err;
        CHECK(mapper.load_file(path, &err), "load .nammidi");
        CHECK(mapper.get_mappings().size() == n, "same number of mappings after load");
        MidiMapper second; // reads QSettings midi/mappings
        CHECK(second.get_mappings().size() == n, "mappings persist in QSettings");

        // A save that can't complete reports failure and leaves the old file intact
        // (review item 06). A read-only folder inside our own temp dir: the atomic
        // save can't create its temporary there, so nothing may touch the file.
        const QString locked_dir = tmp.path() + "/locked";
        QDir().mkpath(locked_dir);
        const QString kept = locked_dir + "/kept.nammidi";
        CHECK(mapper.save_file(kept), "save before locking the folder");
        QFile before(kept);
        CHECK(before.open(QIODevice::ReadOnly), "read the saved file");
        const QByteArray original = before.readAll();
        before.close();
        QFile::setPermissions(locked_dir, QFile::ReadOwner | QFile::ExeOwner);
        // Root (CI's container jobs) writes into a read-only folder regardless, so
        // there is nothing to check there (audit 2026-10-04): probe first.
        QFile probe(locked_dir + "/probe");
        const bool lock_holds = !probe.open(QIODevice::WriteOnly);
        probe.close();
        probe.remove();
        if (lock_holds) {
            mapper.set_mappings(MidiMapper::default_mappings()); // different content
            const bool saved = mapper.save_file(kept);
            QFile after(kept);
            CHECK(after.open(QIODevice::ReadOnly), "the file is still there");
            CHECK(!saved, "save into a read-only folder reports failure");
            CHECK(after.readAll() == original, "the existing file is left as it was");
        } else {
            std::printf("  skip read-only folder: this user can write into it\n");
        }
        QFile::setPermissions(locked_dir, QFile::ReadOwner | QFile::WriteOwner |
                                              QFile::ExeOwner); // so tmp can clean up
    }

    std::printf("MIDI clock → tempo\n");
    {
        double bpm = 0.0;
        QObject::connect(&mapper, &MidiMapper::tempo_from_clock, [&bpm](double b) {
            bpm = b;
        });
        mapper.set_follow_clock(true);
        // 125 BPM: a quarter = 480 ms, a clock tick = 20 ms.
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < 24 * 3 + 1; ++i) {
            while (t.elapsed() < i * 20) {
                QThread::usleep(200);
            }
            mapper.handle({MidiMessage::CLOCK, 0, 0, 0});
        }
        CHECK(std::abs(bpm - 125.0) < 1.5, qPrintable(QString("clock at 125 BPM → %1").arg(bpm)));
    }

    std::printf(gFailures ? "\n%d FAILED\n" : "\nall passed\n", gFailures);
    return gFailures ? 1 : 0;
}

} // namespace namx

int main(int argc, char** argv) {
    return namx::run(argc, argv);
}
