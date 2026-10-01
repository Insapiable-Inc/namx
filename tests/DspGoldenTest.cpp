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
#include "CompressorProcessor.h"
#include "DelayProcessor.h"
#include "Denormals.h"
#include "DoublerProcessor.h"
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

static constexpr double SAMPLE_RATE = 48000.0;
static constexpr size_t TOTAL_FRAMES = 96000; // 2 s
// Irregular block sizes, cycled: exercises partial blocks and chunking.
static constexpr size_t BLOCK_SIZES[] = {128, 64, 37, 256, 1, 200};

// Recorded from the code at the start of Phase 20 (before DelayLine).
static const std::map<std::string, std::uint64_t> GOLDEN = {
    {"cabinet/mono", 0x580a0e82b8397b41ull},
    {"cabinet/stereo", 0x93c47996373aa3c7ull},
    {"chorus-0/mono", 0x0f9a5ea4da5511abull},
    {"chorus-0/stereo", 0xae2eb375716deb27ull},
    {"chorus-1/mono", 0xb83890ab3d2bcd8bull},
    {"chorus-1/stereo", 0x0e3a7eeeddf05c7full},
    {"chorus-2/mono", 0x3b818da16f56d268ull},
    {"chorus-2/stereo", 0x10be8091d9f41b80ull},
    {"chorus-3/mono", 0x1eb8f57ee4a12a39ull},
    {"chorus-3/stereo", 0xae2eb375716deb27ull},
    {"chorus-mono-mode/mono", 0x3d140461a8eb6df1ull},
    {"chorus-mono-mode/stereo", 0xae2eb375716deb27ull},
    {"compressor/mono", 0x16400d75fe01cb2dull},
    {"compressor/stereo", 0xe2a2a9e6e3ebe7d1ull},
    {"delay-crystals/mono", 0xf3c47d48c9e0db17ull},
    {"delay-crystals/stereo", 0x1172aa35a5d21664ull},
    {"delay-stereo-mod-sat/mono", 0x452feb609bbd43a8ull},
    {"delay-stereo-mod-sat/stereo", 0x8d406d0d286cd103ull},
    {"delay/mono", 0x78f0426cf3c35cadull},
    {"delay/stereo", 0x918f8f162e6ea7b2ull},
    {"doubler/mono", 0xac9d0d1465f2754aull},
    {"doubler/stereo", 0xb4c6248f4f5cc715ull},
    {"drive/mono", 0x735229383a5a3f31ull},
    {"drive/stereo", 0xc9dba72e9bc7abe9ull},
    {"eq/mono", 0x454f5b66d4a3d135ull},
    {"eq/stereo", 0x6bb6116234213fc9ull},
    {"flanger-stereo/mono", 0x4968154d937e7279ull},
    {"flanger-stereo/stereo", 0x906f2a9e20e9645aull},
    {"flanger/mono", 0x95d1af4d7940ba95ull},
    {"flanger/stereo", 0x48d4e3fe524a5637ull},
    {"gate/mono", 0xab6881363a899381ull},
    {"gate/stereo", 0x2d537811b7610f95ull},
    {"limiter", 0x0136dae446dc2881ull},
    {"phaser-vibe-stereo/mono", 0x8da13e2193258d72ull},
    {"phaser-vibe-stereo/stereo", 0x6cba34bf9debfec7ull},
    {"phaser/mono", 0x95df8bc0228135cdull},
    {"phaser/stereo", 0x45a0e03a43eb6929ull},
    {"reverb-stereo-shimmer/mono", 0xa486ba1a607ffb71ull},
    {"reverb-stereo-shimmer/stereo", 0x89832d36815ba315ull},
    {"reverb/mono", 0x7f01dde42cd35dedull},
    {"reverb/stereo", 0x68c808e69098118eull},
    {"transpose-down/mono", 0xc21f95bce0c136f5ull},
    {"transpose-down/stereo", 0x04eda4b0bcb638aeull},
    {"transpose-up/mono", 0xbe223e74b49b4069ull},
    {"transpose-up/stereo", 0x70e176f31ed6a35aull},
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
    std::vector<float> left(256);
    std::vector<float> right(256);
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
    std::vector<std::int16_t> samples(LENGTH);
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
    std::map<std::string, Case> cases;
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
    auto both = [this](const std::string& name, auto make) {
        for (const bool stereo_input : {false, true}) {
            cases[name + (stereo_input ? "/stereo" : "/mono")] = [make, stereo_input] {
                auto fx = make();
                return render_effect(*fx, stereo_input);
            };
        }
    };

    both("gate", [] {
        auto fx = std::make_unique<NoiseGateProcessor>();
        fx->set_threshold(-40.0f);
        return fx;
    });
    both("compressor", [] {
        auto fx = std::make_unique<CompressorProcessor>();
        fx->set_threshold(-24.0f);
        fx->set_ratio(4.0f);
        fx->set_makeup(6.0f);
        return fx;
    });
    both("wah", [] {
        return std::make_unique<DynamicWahProcessor>();
    });
    both("wah-pedal", [] {
        auto fx = std::make_unique<DynamicWahProcessor>();
        fx->set_pedal_mode(true);
        fx->set_position(70.0f);
        return fx;
    });
    both("flanger", [] {
        auto fx = std::make_unique<FlangerProcessor>();
        fx->set_feedback(60.0f);
        return fx;
    });
    both("flanger-stereo", [] {
        auto fx = std::make_unique<FlangerProcessor>();
        fx->set_stereo_mode(true);
        return fx;
    });
    both("phaser", [] {
        return std::make_unique<PhaserProcessor>();
    });
    both("phaser-vibe-stereo", [] {
        auto fx = std::make_unique<PhaserProcessor>();
        fx->set_vibe(true);
        fx->set_stereo_mode(true);
        return fx;
    });
    both("drive", [] {
        auto fx = std::make_unique<DriveProcessor>();
        fx->set_drive(70.0f);
        return fx;
    });
    both("eq", [] {
        auto fx = std::make_unique<EQProcessor>();
        fx->set_low_gain(4.0);
        fx->set_mid_gain(-3.0);
        fx->set_treble_gain(2.0);
        return fx;
    });
    both("doubler", [] {
        return std::make_unique<DoublerProcessor>();
    });
    for (int mode = ChorusProcessor::NORMAL; mode <= ChorusProcessor::SPATIAL; ++mode) {
        both("chorus-" + std::to_string(mode), [mode] {
            auto fx = std::make_unique<ChorusProcessor>();
            fx->set_mode(mode);
            fx->set_stereo_mode(true);
            return fx;
        });
    }
    both("chorus-mono-mode", [] {
        return std::make_unique<ChorusProcessor>();
    });
    both("tremolo", [] {
        return std::make_unique<TremoloProcessor>();
    });
    both("tremolo-stereo", [] {
        auto fx = std::make_unique<TremoloProcessor>();
        fx->set_stereo_mode(true);
        return fx;
    });
    both("delay", [] {
        auto fx = std::make_unique<DelayProcessor>();
        fx->set_time(180.0f);
        fx->set_feedback(55.0f);
        fx->set_mix(40.0f);
        return fx;
    });
    both("delay-stereo-mod-sat", [] {
        auto fx = std::make_unique<DelayProcessor>();
        fx->set_stereo_mode(true);
        fx->set_time(240.0f);
        fx->set_modulation(50.0f);
        fx->set_saturation(50.0f);
        fx->set_cross_feed(30.0f);
        return fx;
    });
    both("delay-crystals", [] {
        auto fx = std::make_unique<DelayProcessor>();
        fx->set_stereo_mode(true);
        fx->set_crystals(60.0f);
        return fx;
    });
    both("reverb", [] {
        auto fx = std::make_unique<ReverbProcessor>();
        fx->set_pre_delay(30.0f);
        return fx;
    });
    both("reverb-stereo-shimmer", [] {
        auto fx = std::make_unique<ReverbProcessor>();
        fx->set_stereo_mode(true);
        fx->set_shimmer(60.0f);
        return fx;
    });
    both("transpose-down", [] {
        auto fx = std::make_unique<TransposeProcessor>();
        fx->set_semitones(-2.0f);
        return fx;
    });
    both("transpose-up", [] {
        auto fx = std::make_unique<TransposeProcessor>();
        fx->set_semitones(12.0f);
        fx->set_mix(50.0f);
        return fx;
    });
    both("volume", [] {
        auto fx = std::make_unique<VolumeProcessor>();
        fx->set_position(40.0f);
        return fx;
    });
    both("cabinet", [ir_a, ir_b] {
        auto fx = std::make_unique<IRProcessor>();
        fx->set_sample_rate(SAMPLE_RATE);
        const bool loaded = fx->load(ir_a.toStdString()) && fx->load_b(ir_b.toStdString());
        fx->reclaim_retired(true);
        fx->set_blend(35.0f);
        fx->set_pan(1, 60.0f);
        return loaded ? std::move(fx) : nullptr;
    });

    cases["limiter"] = [] {
        OutputLimiter limiter;
        limiter.set_sample_rate(SAMPLE_RATE);
        limiter.set_enabled(true);
        return render(false, [&limiter](float* left, float* right, size_t frames) {
            // Drive it 18 dB hot so it actually limits.
            for (size_t i = 0; i < frames; ++i) {
                left[i] *= 8.0f;
                right[i] *= 8.0f;
            }
            limiter.process(left, right, frames);
        });
    };
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
    std::vector<float> block(256);
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
        pitch = tuner.last_pitch();
    }
    QVERIFY2(std::abs(pitch - 110.0f) <= 0.2f, qPrintable(QString::number(pitch)));
}

QTEST_GUILESS_MAIN(DspGoldenTest)
#include "DspGoldenTest.moc"
