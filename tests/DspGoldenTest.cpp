// Golden-output test for the DSP effects: every effect renders a fixed synthetic
// signal with fixed settings, and a hash of the exact output bits must match the
// value recorded below. It guards refactors that must not change the sound (Phase 20:
// DelayLine, size_t indices, renames): any change to the arithmetic changes a hash.
//
// When a change is MEANT to alter the sound, re-record the table:
//   NAM_GOLDEN_PRINT=1 ./DspGoldenTest
// and paste the printed lines over GOLDEN below.
//
// Deterministic by construction: the input is a sum of sines with fixed envelopes,
// the IR is a fixed decaying cosine, and nothing reads the clock or random sources.
// The only file written is the IR, inside a QTemporaryDir.

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <numbers>
#include <string>
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

// Recorded from the code at the start of Phase 20 (before DelayLine).
static const std::map<string, std::uint64_t> GOLDEN = {
    {"cabinet/mono", 0x580a0e82b8397b41ull},
    {"cabinet/stereo", 0x93c47996373aa3c7ull},
    {"chorus-0/mono", 0xf0234e463d73a65aull},
    {"chorus-0/stereo", 0x25f5016796d7189cull},
    {"chorus-1/mono", 0xcc59c79019def1acull},
    {"chorus-1/stereo", 0x03463f3c84ebd8c4ull},
    {"chorus-2/mono", 0x60f20c4f7ae79ea4ull},
    {"chorus-2/stereo", 0xce952092ea7a780full},
    {"chorus-3/mono", 0x00e03a45c4ef09f8ull},
    {"chorus-3/stereo", 0x25f5016796d7189cull},
    {"chorus-mono-mode/mono", 0xf8b548e4ff56c311ull},
    {"chorus-mono-mode/stereo", 0x25f5016796d7189cull},
    {"compressor/mono", 0x16400d75fe01cb2dull},
    {"compressor/stereo", 0xe2a2a9e6e3ebe7d1ull},
    {"delay-crystals/mono", 0xc93f7f6ff621338aull},
    {"delay-crystals/stereo", 0x79c50102f62a50bbull},
    {"delay-stereo-mod-sat/mono", 0x0617596bbd88afb7ull},
    {"delay-stereo-mod-sat/stereo", 0xb7dfb4e8d47a5ee1ull},
    {"delay/mono", 0x78f0426cf3c35cadull},
    {"delay/stereo", 0x918f8f162e6ea7b2ull},
    {"drive/mono", 0x735229383a5a3f31ull},
    {"drive/stereo", 0xc9dba72e9bc7abe9ull},
    {"eq/mono", 0x454f5b66d4a3d135ull},
    {"eq/stereo", 0x6bb6116234213fc9ull},
    {"flanger-stereo/mono", 0xbf7a61437dde2633ull},
    {"flanger-stereo/stereo", 0x8c4c0e8eea811b90ull},
    {"flanger/mono", 0xdfc562c48b73d049ull},
    {"flanger/stereo", 0x16767fbcf4380fa2ull},
    {"gate/mono", 0xab6881363a899381ull},
    {"gate/stereo", 0x2d537811b7610f95ull},
    {"limiter", 0x0136dae446dc2881ull},
    {"phaser-vibe-stereo/mono", 0x8da13e2193258d72ull},
    {"phaser-vibe-stereo/stereo", 0x6cba34bf9debfec7ull},
    {"phaser/mono", 0x95df8bc0228135cdull},
    {"phaser/stereo", 0x45a0e03a43eb6929ull},
    {"reverb-stereo-shimmer/mono", 0x1dbc41a288ee02f3ull},
    {"reverb-stereo-shimmer/stereo", 0xad67b6bf9e1382f6ull},
    {"reverb/mono", 0xba399f92706cc369ull},
    {"reverb/stereo", 0x6f0e72b5de0c8581ull},
    {"transpose-down/mono", 0xadc68edcd84f8e45ull},
    {"transpose-down/stereo", 0xbf8288a519dbad74ull},
    {"transpose-up/mono", 0x0f9a8dc7d21f009dull},
    {"transpose-up/stereo", 0xbc6c9b1fe5654a5eull},
    {"tremolo-stereo/mono", 0xaf15c0b379c3fdb0ull},
    {"tremolo-stereo/stereo", 0xcabf380bd28ab05cull},
    {"tremolo/mono", 0xc221fd4f5d497a5dull},
    {"tremolo/stereo", 0xcabf380bd28ab05cull},
    {"volume/mono", 0xb823ca0c94e93179ull},
    {"volume/stereo", 0xcdebe42bf5f5a1b7ull},
    {"wah-pedal/mono", 0x6348ba2bd3941e75ull},
    {"wah-pedal/stereo", 0x42338be964ecfffeull},
    {"wah/mono", 0x1e854b8c9962da75ull},
    {"wah/stereo", 0x9eacba8fbd7a37b4ull},
};

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

// FNV-1a over the bit patterns of the samples.
static void hash_samples(std::uint64_t& hash, const float* samples, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        std::uint32_t bits = std::bit_cast<std::uint32_t>(samples[i]);
        for (int byte = 0; byte < 4; ++byte) {
            hash ^= bits & 0xffu;
            hash *= 0x100000001b3ull;
            bits >>= 8;
        }
    }
}

// Runs `process` over the whole signal in irregular blocks and hashes L and R.
// Halfway through, `on_halfway` runs (used to exercise reset()).
static std::uint64_t render(bool stereo_input,
                            const std::function<void(float*, float*, size_t)>& process,
                            const std::function<void()>& on_halfway = {}) {
    std::uint64_t hash = 0xcbf29ce484222325ull;
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
        hash_samples(hash, left.data(), frames);
        hash_samples(hash, right.data(), frames);
        frame += frames;
        ++block;
        if (!halfway_done && frame >= TOTAL_FRAMES / 2) {
            halfway_done = true;
            if (on_halfway) {
                on_halfway();
            }
        }
    }
    return hash;
}

// Renders one effect, with a reset() halfway through.
static std::uint64_t render_effect(EffectBase& fx, bool stereo_input) {
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

static unique_ptr<PhaserProcessor> make_phaser_vibe_stereo() {
    auto fx = make_unique<PhaserProcessor>();
    fx->set_vibe(true);
    fx->set_stereo_mode(true);
    return fx;
}

static unique_ptr<DriveProcessor> make_drive() {
    auto fx = make_unique<DriveProcessor>();
    fx->set_drive_pct(70.0f);
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

static unique_ptr<DelayProcessor> make_delay() {
    auto fx = make_unique<DelayProcessor>();
    fx->set_time(180.0f);
    fx->set_feedback_pct(55.0f);
    fx->set_mix_pct(40.0f);
    return fx;
}

static unique_ptr<DelayProcessor> make_delay_stereo_mod_sat() {
    auto fx = make_unique<DelayProcessor>();
    fx->set_stereo_mode(true);
    fx->set_time(240.0f);
    fx->set_modulation(50.0f);
    fx->set_saturation(50.0f);
    fx->set_cross_feed(30.0f);
    return fx;
}

static unique_ptr<DelayProcessor> make_delay_crystals() {
    auto fx = make_unique<DelayProcessor>();
    fx->set_stereo_mode(true);
    fx->set_crystals(60.0f);
    return fx;
}

static unique_ptr<ReverbProcessor> make_reverb() {
    auto fx = make_unique<ReverbProcessor>();
    fx->set_pre_delay(30.0f);
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

static std::uint64_t render_limiter() {
    OutputLimiter limiter;
    limiter.set_sample_rate(SAMPLE_RATE);
    limiter.set_enabled(true);
    return render(false, [&limiter](float* left, float* right, size_t frames) {
        drive_and_limit(limiter, left, right, frames);
    });
}

class DspGoldenTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void effects_data();
    void effects();
    void tuner_detects_pitch();

private:
    using Case = std::function<std::uint64_t()>;
    std::map<string, Case> cases;
    QTemporaryDir temp_dir;
    bool print_mode = false;
};

void DspGoldenTest::initTestCase() {
    enable_flush_to_zero(); // same MXCSR as the backends' audio threads
    print_mode = qEnvironmentVariableIsSet("NAM_GOLDEN_PRINT");
    QVERIFY(temp_dir.isValid());
    const QString ir_a = temp_dir.filePath("ir_a.wav");
    const QString ir_b = temp_dir.filePath("ir_b.wav");
    QVERIFY(write_ir(ir_a, 120.0, 0.21));
    QVERIFY(write_ir(ir_b, 300.0, 0.05));

    // Each effect: once with mono input, once with stereo input.
    auto both = [this](const string& name, auto make) {
        for (const bool stereo_input : {false, true}) {
            cases[name + (stereo_input ? "/stereo" : "/mono")] = [make, stereo_input] {
                return render_effect(*make(), stereo_input);
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
    both("phaser-vibe-stereo", &make_phaser_vibe_stereo);
    both("drive", &make_drive);
    both("eq", &make_eq);
    for (int mode = ChorusProcessor::NORMAL; mode <= ChorusProcessor::SPATIAL; ++mode) {
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
    both("delay", &make_delay);
    both("delay-stereo-mod-sat", &make_delay_stereo_mod_sat);
    both("delay-crystals", &make_delay_crystals);
    both("reverb", &make_reverb);
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
    const std::uint64_t hash = cases.at(name.toStdString())();
    if (print_mode) {
        std::printf("    {\"%s\", 0x%016llxull},\n", qPrintable(name),
                    static_cast<unsigned long long>(hash));
        return;
    }
    const auto expected = GOLDEN.find(name.toStdString());
    QVERIFY2(expected != GOLDEN.end(), "no golden value recorded for this case");
    QCOMPARE(hash, expected->second);
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

} // namespace namx

QTEST_GUILESS_MAIN(namx::DspGoldenTest)
#include "DspGoldenTest.moc"
