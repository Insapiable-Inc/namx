// Golden-output test for the DSP effects: every effect renders a fixed synthetic
// signal with fixed settings, and a signature of the output must match the one
// recorded in tests/dsp-golden.txt. It guards refactors that must not change the sound
// (Phase 20: DelayLine, size_t indices, renames).
//
// The signature, per quarter second and channel: RMS, and the level and phase at
// 440.5 Hz (the test signal's main partial) and 2 kHz. Values must match within 0.1 %
// of that window's RMS. Not bit-exact on purpose: system math libraries round sinf,
// tanf… differently between versions (glibc 2.39 vs 2.44 changed tanf), which moves
// the output by ~1e-7 — while a one-sample shift moves the 440 Hz phase by ~6 %.
//
// When a change is MEANT to alter the sound, re-record the file (from the repo root):
//   NAM_GOLDEN_PRINT=1 build-test/DspGoldenTest | sed -n 's/^golden: //p' > tests/dsp-golden.txt
//
// Deterministic by construction: the input is a sum of sines with fixed envelopes,
// the IR is a fixed decaying cosine, and nothing reads the clock or random sources.
// The only file written is the IR, inside a QTemporaryDir.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <numbers>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "ChorusProcessor.h"
#include "Common.h"
#include "CompressorProcessor.h"
#include "DelayProcessor.h"
#include "Denormals.h"
#include "DriveProcessor.h"
#include "DynamicWahProcessor.h"
#include "EQProcessor.h"
#include "FlangerProcessor.h"
#include "IRProcessor.h"
#include "NoiseGateProcessor.h"
#include "OutputLimiter.h"
#include "PhaserProcessor.h"
#include "ReverbProcessor.h"
#include "TransposeProcessor.h"
#include "TremoloProcessor.h"
#include "TunerProcessor.h"
#include "VolumeProcessor.h"

namespace namx
{

static constexpr double SAMPLE_RATE = 48000.0;
static constexpr size_t TOTAL_FRAMES = 96000; // 2 s
// Irregular block sizes, cycled: exercises partial blocks and chunking.
static constexpr size_t BLOCK_SIZES[] = {128, 64, 37, 256, 1, 200};

// The signature: per window, per channel (L then R), these values.
static constexpr size_t WINDOWS = 8; // quarter seconds
static constexpr size_t WINDOW_FRAMES = TOTAL_FRAMES / WINDOWS;
static constexpr double PROBE_HZ[] = {440.5, 2000.0};
static constexpr size_t VALUES_PER_CHANNEL = 1 + 2 * std::size(PROBE_HZ); // RMS, re/im…
static constexpr size_t VALUES_PER_WINDOW = 2 * VALUES_PER_CHANNEL;
using Signature = vector<double>; // WINDOWS × VALUES_PER_WINDOW

// Allowed difference: 0.1 % of the window's RMS, and never below -120 dBFS (silence).
static constexpr double RELATIVE_TOLERANCE = 1e-3;
static constexpr double ABSOLUTE_TOLERANCE = 1e-6;

// Guitar-like test signal: plucks every 0.5 s on two partials, silent for the last
// quarter of each second (so the gate and the reverb/delay tails get exercised).
static float input_left(size_t frame) {
    const double t = static_cast<double>(frame) / SAMPLE_RATE;
    const double in_second = std::fmod(t, 1.0);
    if (in_second >= 0.75) {
        return 0.0f;
    }
    const double since_pluck = std::fmod(t, 0.5);
    const double envelope = std::exp(-since_pluck * 6.0);
    const double two_pi = 2.0 * std::numbers::pi;
    const double x = 0.3 * std::sin(two_pi * 110.0 * t) + 0.15 * std::sin(two_pi * 440.5 * t);
    return static_cast<float>(envelope * x);
}

// A different right channel, for the stereo-input cases.
static float input_right(size_t frame) {
    const double t = static_cast<double>(frame) / SAMPLE_RATE;
    const double two_pi = 2.0 * std::numbers::pi;
    return static_cast<float>(0.25 * std::sin(two_pi * 165.0 * t) * std::exp(-std::fmod(t, 0.7)));
}

// Running sums for one window of one channel.
struct WindowSums
{
    double squares = 0.0;
    array<double, std::size(PROBE_HZ)> re{}, im{};

    void add(const float* samples, size_t first_frame, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            const double x = samples[i];
            const double t = static_cast<double>(first_frame + i) / SAMPLE_RATE;
            squares += x * x;
            for (size_t k = 0; k < std::size(PROBE_HZ); ++k) {
                const double phase = 2.0 * std::numbers::pi * PROBE_HZ[k] * t;
                re[k] += x * std::cos(phase);
                im[k] += x * std::sin(phase);
            }
        }
    }

    // RMS, then each probe's re/im as an amplitude (a full-scale sine there → 1).
    void append_to(Signature& signature) const {
        const auto frames = static_cast<double>(WINDOW_FRAMES);
        signature.push_back(std::sqrt(squares / frames));
        for (size_t k = 0; k < std::size(PROBE_HZ); ++k) {
            signature.push_back(2.0 * re[k] / frames);
            signature.push_back(2.0 * im[k] / frames);
        }
    }
};

// Runs `process` over the whole signal in irregular blocks and returns the signature.
// Halfway through, `on_halfway` runs (used to exercise reset()).
static Signature render(bool stereo_input,
                        const std::function<void(float*, float*, size_t)>& process,
                        const std::function<void()>& on_halfway = {}) {
    Signature signature;
    WindowSums sums_left, sums_right;
    vector<float> left(256);
    vector<float> right(256);
    size_t frame = 0;
    size_t block = 0;
    bool halfway_done = false;
    while (frame < TOTAL_FRAMES) {
        const size_t frames =
            std::min(BLOCK_SIZES[block % std::size(BLOCK_SIZES)], TOTAL_FRAMES - frame);
        for (size_t i = 0; i < frames; ++i) {
            left[i] = input_left(frame + i);
            right[i] = stereo_input ? input_right(frame + i) : left[i];
        }
        process(left.data(), right.data(), frames);
        // Blocks may straddle a window boundary: split them there.
        for (size_t done = 0; done < frames;) {
            const size_t start = frame + done;
            const size_t chunk = std::min(frames - done, WINDOW_FRAMES - start % WINDOW_FRAMES);
            sums_left.add(left.data() + done, start, chunk);
            sums_right.add(right.data() + done, start, chunk);
            done += chunk;
            if ((start + chunk) % WINDOW_FRAMES == 0) {
                sums_left.append_to(signature);
                sums_right.append_to(signature);
                sums_left = {};
                sums_right = {};
            }
        }
        frame += frames;
        ++block;
        if (!halfway_done && frame >= TOTAL_FRAMES / 2) {
            halfway_done = true;
            if (on_halfway) {
                on_halfway();
            }
        }
    }
    return signature;
}

// Renders one effect, with a reset() halfway through.
static Signature render_effect(EffectBase& fx, bool stereo_input) {
    fx.set_sample_rate(SAMPLE_RATE);
    fx.set_enabled(true);
    return render(
        stereo_input,
        [&fx, stereo_input](float* left, float* right, size_t frames) {
            fx.process(left, right, frames, stereo_input);
        },
        [&fx] {
            fx.reset();
        });
}

// 16-bit mono WAV with a fixed decaying-cosine impulse response.
static bool write_ir(const QString& path, double decay_samples, double frequency) {
    constexpr std::uint32_t LENGTH = 2048;
    vector<std::int16_t> samples(LENGTH);
    for (std::uint32_t n = 0; n < LENGTH; ++n) {
        const double value =
            (n == 0 ? 0.5 : 0.0) + 0.4 * std::exp(-n / decay_samples) * std::cos(frequency * n);
        samples[n] = static_cast<std::int16_t>(std::lround(value * 32767.0));
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    auto put_u32 = [&file](std::uint32_t v) {
        file.write(reinterpret_cast<const char*>(&v), 4); // little-endian hosts only
    };
    auto put_u16 = [&file](std::uint16_t v) {
        file.write(reinterpret_cast<const char*>(&v), 2);
    };
    const std::uint32_t data_bytes = LENGTH * 2;
    file.write("RIFF", 4);
    put_u32(36 + data_bytes);
    file.write("WAVEfmt ", 8);
    put_u32(16);
    put_u16(1); // PCM
    put_u16(1); // mono
    put_u32(48000);
    put_u32(48000 * 2);
    put_u16(2);
    put_u16(16);
    file.write("data", 4);
    put_u32(data_bytes);
    file.write(reinterpret_cast<const char*>(samples.data()), data_bytes);
    return true;
}

// Effect configurations rendered by the cases below.
static unique_ptr<NoiseGateProcessor> make_gate() {
    auto fx = make_unique<NoiseGateProcessor>();
    fx->set_threshold(-40.0f);
    return fx;
}

static unique_ptr<CompressorProcessor> make_compressor() {
    auto fx = make_unique<CompressorProcessor>();
    fx->set_threshold(-24.0f);
    fx->set_ratio(4.0f);
    fx->set_makeup(6.0f);
    return fx;
}

static unique_ptr<DynamicWahProcessor> make_wah_pedal() {
    auto fx = make_unique<DynamicWahProcessor>();
    fx->set_pedal_mode(true);
    fx->set_position_pct(70.0f);
    return fx;
}

static unique_ptr<FlangerProcessor> make_flanger() {
    auto fx = make_unique<FlangerProcessor>();
    fx->set_feedback_pct(60.0f);
    return fx;
}

static unique_ptr<FlangerProcessor> make_flanger_stereo() {
    auto fx = make_unique<FlangerProcessor>();
    fx->set_stereo_mode(true);
    return fx;
}

static unique_ptr<PhaserProcessor> make_phaser_feedback() {
    auto fx = make_unique<PhaserProcessor>();
    fx->set_rate_hz(1.0f);
    fx->set_depth_pct(80.0f);
    fx->set_feedback_pct(70.0f);
    return fx;
}

static unique_ptr<DriveProcessor> make_drive() {
    auto fx = make_unique<DriveProcessor>();
    fx->set_drive_pct(70.0f);
    // Recorded with these two (the constructor's defaults at the time; they now
    // follow Constants.h, like the card).
    fx->set_tone_pct(50.0f);
    fx->set_level(0.0f);
    return fx;
}

static unique_ptr<EQProcessor> make_eq() {
    auto fx = make_unique<EQProcessor>();
    fx->set_low_gain(4.0);
    fx->set_mid_gain(-3.0);
    fx->set_treble_gain(2.0);
    return fx;
}

static unique_ptr<TremoloProcessor> make_tremolo_stereo() {
    auto fx = make_unique<TremoloProcessor>();
    fx->set_stereo_mode(true);
    return fx;
}

static unique_ptr<TremoloProcessor> make_tremolo_tr2(bool stereo) {
    auto fx = make_unique<TremoloProcessor>();
    fx->set_type(TremoloProcessor::TR2);
    fx->set_rate_hz(7.0f);
    fx->set_depth_pct(85.0f);
    fx->set_wave_pct(70.0f);
    fx->set_stereo_mode(stereo);
    return fx;
}

static unique_ptr<DelayProcessor> make_delay() {
    auto fx = make_unique<DelayProcessor>();
    fx->set_time(180.0f);
    fx->set_right_time(180.0f);
    fx->set_feedback_pct(55.0f);
    fx->set_mix_pct(40.0f);
    return fx;
}

static unique_ptr<DelayProcessor> make_delay_stereo_mod_sat() {
    auto fx = make_unique<DelayProcessor>();
    fx->set_stereo_mode(true);
    fx->set_time(240.0f);
    fx->set_right_time(240.0f);
    fx->set_modulation(50.0f);
    fx->set_saturation(50.0f);
    fx->set_cross_feed(30.0f);
    return fx;
}

static unique_ptr<DelayProcessor> make_delay_shimmer() {
    auto fx = make_unique<DelayProcessor>();
    fx->set_stereo_mode(true);
    fx->set_shimmer_pct(60.0f);
    return fx;
}

static unique_ptr<ReverbProcessor> make_reverb() {
    auto fx = make_unique<ReverbProcessor>();
    fx->set_pre_delay(30.0f);
    return fx;
}

static unique_ptr<ReverbProcessor> make_reverb_hall() {
    auto fx = make_unique<ReverbProcessor>();
    fx->set_type(static_cast<int>(ReverbProcessor::Type::HALL));
    fx->set_stereo_mode(true);
    return fx;
}

static unique_ptr<ReverbProcessor> make_reverb_room() {
    auto fx = make_unique<ReverbProcessor>();
    fx->set_type(static_cast<int>(ReverbProcessor::Type::ROOM));
    fx->set_pre_delay(120.0f);
    return fx;
}

static unique_ptr<ReverbProcessor> make_reverb_stereo_shimmer() {
    auto fx = make_unique<ReverbProcessor>();
    fx->set_stereo_mode(true);
    fx->set_shimmer_pct(60.0f);
    return fx;
}

static unique_ptr<TransposeProcessor> make_transpose_down() {
    auto fx = make_unique<TransposeProcessor>();
    fx->set_semitones(-2.0f);
    return fx;
}

static unique_ptr<TransposeProcessor> make_transpose_up() {
    auto fx = make_unique<TransposeProcessor>();
    fx->set_semitones(12.0f);
    fx->set_mix_pct(50.0f);
    return fx;
}

static unique_ptr<VolumeProcessor> make_volume() {
    auto fx = make_unique<VolumeProcessor>();
    fx->set_position_pct(40.0f);
    return fx;
}

static unique_ptr<ChorusProcessor> make_chorus(int mode) {
    auto fx = make_unique<ChorusProcessor>();
    fx->set_mode(mode);
    fx->set_stereo_mode(true);
    return fx;
}

// Both cabinet slots loaded with the generated IRs; null if a load fails.
static unique_ptr<IRProcessor> make_cabinet(const QString& ir_a, const QString& ir_b) {
    auto fx = make_unique<IRProcessor>();
    fx->set_sample_rate(SAMPLE_RATE);
    const bool loaded = fx->load(ir_a.toStdString()) && fx->load_b(ir_b.toStdString());
    fx->reclaim_retired(true);
    fx->set_blend_pct(35.0f);
    fx->set_pan(1, 60.0f);
    return loaded ? std::move(fx) : nullptr;
}

// The limiter fed 18 dB hot, so it actually limits.
static void drive_and_limit(OutputLimiter& limiter, float* left, float* right, size_t frames) {
    for (size_t i = 0; i < frames; ++i) {
        left[i] *= 8.0f;
        right[i] *= 8.0f;
    }
    limiter.process(left, right, frames);
}

static Signature render_limiter() {
    OutputLimiter limiter;
    limiter.set_sample_rate(SAMPLE_RATE);
    limiter.set_enabled(true);
    return render(false, [&limiter](float* left, float* right, size_t frames) {
        drive_and_limit(limiter, left, right, frames);
    });
}

// One line per case: the name, then the signature's values.
static std::map<string, Signature> read_golden(const char* path) {
    std::map<string, Signature> table;
    std::ifstream file(path);
    string line;
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        string name;
        fields >> name;
        Signature values;
        for (double value = 0.0; fields >> value;) {
            values.push_back(value);
        }
        if (!name.empty()) {
            table[name] = std::move(values);
        }
    }
    return table;
}

class DspGoldenTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void effects_data();
    void effects();
    void mono_to_stereo_edge();
    void tuner_detects_pitch();

private:
    using Case = std::function<Signature()>;
    std::map<string, Case> cases;
    std::map<string, Signature> golden; // from tests/dsp-golden.txt
    QTemporaryDir temp_dir;
    bool print_mode = false;
};

void DspGoldenTest::initTestCase() {
    enable_flush_to_zero(); // same MXCSR as the backends' audio threads
    print_mode = qEnvironmentVariableIsSet("NAM_GOLDEN_PRINT");
    golden = read_golden(NAM_GOLDEN_FILE);
    QVERIFY2(print_mode || !golden.empty(), "no golden signatures in " NAM_GOLDEN_FILE);
    QVERIFY(temp_dir.isValid());
    const QString ir_a = temp_dir.filePath("ir_a.wav");
    const QString ir_b = temp_dir.filePath("ir_b.wav");
    QVERIFY(write_ir(ir_a, 120.0, 0.21));
    QVERIFY(write_ir(ir_b, 300.0, 0.05));

    // Each effect: once with mono input, once with stereo input.
    auto both = [this](const string& name, auto make) {
        for (const bool stereo_input : {false, true}) {
            cases[name + (stereo_input ? "/stereo" : "/mono")] = [make, stereo_input, name] {
                auto fx = make();
                if (!fx) {
                    qFatal("%s: the effect could not be built (an IR failed to load?)",
                           name.c_str());
                }
                return render_effect(*fx, stereo_input);
            };
        }
    };

    both("gate", &make_gate);
    both("compressor", &make_compressor);
    both("wah", [] {
        return make_unique<DynamicWahProcessor>();
    });
    both("wah-pedal", &make_wah_pedal);
    both("flanger", &make_flanger);
    both("flanger-stereo", &make_flanger_stereo);
    both("phaser", [] {
        return make_unique<PhaserProcessor>();
    });
    both("phaser-feedback", &make_phaser_feedback);
    both("drive", &make_drive);
    both("eq", &make_eq);
    for (int mode = std::to_underlying(ChorusProcessor::Mode::NORMAL);
         mode <= std::to_underlying(ChorusProcessor::Mode::SPATIAL); ++mode) {
        both("chorus-" + std::to_string(mode), [mode] {
            return make_chorus(mode);
        });
    }
    both("chorus-mono-mode", [] {
        return make_unique<ChorusProcessor>();
    });
    both("tremolo", [] {
        return make_unique<TremoloProcessor>();
    });
    both("tremolo-stereo", &make_tremolo_stereo);
    both("tremolo-tr2", [] {
        return make_tremolo_tr2(false);
    });
    both("tremolo-tr2-stereo", [] {
        return make_tremolo_tr2(true);
    });
    both("delay", &make_delay);
    both("delay-stereo-mod-sat", &make_delay_stereo_mod_sat);
    both("delay-shimmer", &make_delay_shimmer);
    both("reverb", &make_reverb);
    both("reverb-hall", &make_reverb_hall);
    both("reverb-room", &make_reverb_room);
    both("reverb-stereo-shimmer", &make_reverb_stereo_shimmer);
    both("transpose-down", &make_transpose_down);
    both("transpose-up", &make_transpose_up);
    both("volume", &make_volume);
    both("cabinet", [ir_a, ir_b] {
        return make_cabinet(ir_a, ir_b);
    });

    cases["limiter"] = &render_limiter;
}

void DspGoldenTest::effects_data() {
    QTest::addColumn<QString>("name");
    for (const auto& [name, run] : cases) {
        QTest::newRow(name.c_str()) << QString::fromStdString(name);
    }
}

void DspGoldenTest::effects() {
    QFETCH(QString, name);
    const Signature actual = cases.at(name.toStdString())();
    QCOMPARE(actual.size(), WINDOWS * VALUES_PER_WINDOW);
    if (print_mode) {
        // std::format, not printf: always a decimal point, whatever the locale.
        string line = "golden: " + name.toStdString();
        for (const double value : actual) {
            line += std::format(" {:.9g}", value);
        }
        std::printf("%s\n", line.c_str());
        return;
    }
    const auto expected = golden.find(name.toStdString());
    QVERIFY2(expected != golden.end(), "no golden signature recorded for this case");
    QCOMPARE(expected->second.size(), actual.size());
    for (size_t i = 0; i < actual.size(); ++i) {
        const size_t window = i / VALUES_PER_WINDOW;
        const size_t channel = i % VALUES_PER_WINDOW / VALUES_PER_CHANNEL; // 0 = L, 1 = R
        const size_t value = i % VALUES_PER_CHANNEL; // 0 = RMS, then probe re/im
        const size_t rms_index = i - value;
        const double tolerance =
            ABSOLUTE_TOLERANCE + RELATIVE_TOLERANCE * expected->second[rms_index];
        const double difference = std::abs(actual[i] - expected->second[i]);
        QVERIFY2(difference <= tolerance,
                 qPrintable(QString("window %1, %2, value %3: %4, recorded %5")
                                .arg(window)
                                .arg(channel == 0 ? "L" : "R")
                                .arg(value)
                                .arg(actual[i], 0, 'g', 9)
                                .arg(expected->second[i], 0, 'g', 9)));
    }
}

// The tuner's output is a passthrough; what matters is its pitch readout. Its
// detector runs on a worker thread, so wait (bounded) for a steady A2.
void DspGoldenTest::tuner_detects_pitch() {
    TunerProcessor tuner;
    tuner.set_sample_rate(SAMPLE_RATE);
    tuner.set_enabled(true);
    vector<float> block(256);
    size_t frame = 0;
    float pitch = 0.0f;
    for (int attempt = 0; attempt < 200 && std::abs(pitch - 110.0f) > 0.2f; ++attempt) {
        for (int b = 0; b < 20; ++b) {
            for (float& sample : block) {
                const double t = static_cast<double>(frame++) / SAMPLE_RATE;
                sample = static_cast<float>(0.3 * std::sin(2.0 * std::numbers::pi * 110.0 * t));
            }
            tuner.process(block.data(), block.data(), block.size(), false);
        }
        QTest::qWait(10);
        pitch = tuner.get_last_pitch();
    }
    QVERIFY2(std::abs(pitch - 110.0f) <= 0.2f, qPrintable(QString::number(pitch)));
}

// Review item 19: after mono blocks (only the left channel's state advanced), the
// first stereo block must find the right channel in line with the left — for the
// effects whose state is copied exactly, identical L and R input then give identical
// output, not a cold or stale right channel.
void DspGoldenTest::mono_to_stereo_edge() {
    const std::map<string, std::function<unique_ptr<EffectBase>()>> makers = {
        {"gate", make_gate},
        {"compressor", make_compressor},
        {"wah", make_wah_pedal},
        {"flanger", make_flanger},
        {"phaser", make_phaser_feedback},
        {"eq", make_eq},
        {"chorus",
         [] {
             return make_chorus(0);
         }},
    };
    constexpr size_t FRAMES = 128;
    constexpr size_t MONO_BLOCKS = 60; // 160 ms: tails and envelopes well under way
    vector<float> left(FRAMES), right(FRAMES);
    for (const auto& [name, make] : makers) {
        unique_ptr<EffectBase> fx = make();
        fx->set_sample_rate(SAMPLE_RATE);
        fx->set_enabled(true);
        fx->set_stereo_mode(false);
        size_t frame = 0;
        for (size_t b = 0; b < MONO_BLOCKS; ++b, frame += FRAMES) {
            for (size_t i = 0; i < FRAMES; ++i) {
                left[i] = input_left(frame + i);
            }
            fx->process(left.data(), right.data(), FRAMES, false);
        }
        for (size_t i = 0; i < FRAMES; ++i) {
            left[i] = right[i] = input_left(frame + i);
        }
        fx->process(left.data(), right.data(), FRAMES, true);
        float worst = 0.0f;
        for (size_t i = 0; i < FRAMES; ++i) {
            worst = std::max(worst, std::abs(left[i] - right[i]));
        }
        QVERIFY2(worst < 1e-5f,
                 qPrintable(QString("%1: L and R differ by %2 on the first stereo block")
                                .arg(name.c_str())
                                .arg(static_cast<double>(worst))));
    }
}

} // namespace namx

QTEST_GUILESS_MAIN(namx::DspGoldenTest)
#include "DspGoldenTest.moc"
