// NAMPlayerBench — times the NAMPlayer effect chain block by block, the way
// the audio callback runs it, and reports each stage against the real-time
// budget. The Phase 14 efficiency budget is checked with this tool:
//
//   DSP ≤ 10 % of a 128-frame block for the default chain on one core.
//
// Build: cmake -S gui -B gui/build-bench -DCMAKE_BUILD_TYPE=Release -DNAM_BUILD_BENCH=ON
//        cmake --build gui/build-bench --target NAMPlayerBench
// Run:   gui/build-bench/NAMPlayerBench --model amp.nam --ir cab.wav [options]
//
// Every effect is enabled with its default settings (stereo effects in their
// default stereo mode) and fed white noise at -12 dBFS, so data-dependent
// paths (gate open, compressor working) are exercised.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <NAM/activations.h>

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
#include "NamProcessor.h"
#include "NoiseGateProcessor.h"
#include "OutputLimiter.h"
#include "PhaserProcessor.h"
#include "ReverbProcessor.h"
#include "TransposeProcessor.h"
#include "TremoloProcessor.h"
#include "TunerProcessor.h"
#include "VolumeProcessor.h"

namespace
{

struct Stage
{
    const char* name;
    EffectBase* fx;
    std::vector<double> us; // per-block time
};

void usage() {
    std::puts("usage: NAMPlayerBench [--model file.nam] [--ir file.wav] [--ir-b file.wav] "
              "[--frames N] [--rate HZ]\n"
              "                      [--blocks N] [--no-fast-tanh] [--only NAME[,NAME…]]\n"
              "Without --model/--ir the amp/cab stages are skipped. --only limits the\n"
              "enabled stages (names as printed, case-insensitive).");
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    const size_t last = v.size() - 1;
    const size_t i = std::min(last, static_cast<size_t>(p * static_cast<double>(last) + 0.5));
    return v[i];
}

double mean(const std::vector<double>& v) {
    double sum = 0.0;
    for (double x : v) {
        sum += x;
    }
    return sum / static_cast<double>(v.size());
}

bool listed(const std::string& only, const char* name) {
    if (only.empty()) {
        return true;
    }
    std::string hay = "," + only + ",", needle = std::string(",") + name + ",";
    auto lower = [](std::string s) {
        for (auto& c : s) {
            c = static_cast<char>(std::tolower(c));
        }
        return s;
    };
    return lower(hay).find(lower(needle)) != std::string::npos;
}

} // namespace

int main(int argc, char** argv) {
    std::string model, ir, ir_b, only;
    size_t frames = 128;
    double rate = 48000.0;
    size_t blocks = 4000;
    bool fast_tanh = true;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                usage();
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--model") {
            model = next();
        } else if (a == "--ir") {
            ir = next();
        } else if (a == "--ir-b") {
            ir_b = next();
        } else if (a == "--frames") {
            frames = std::strtoul(next(), nullptr, 10);
        } else if (a == "--rate") {
            rate = std::atof(next());
        } else if (a == "--blocks") {
            blocks = std::strtoul(next(), nullptr, 10);
        } else if (a == "--only") {
            only = next();
        } else if (a == "--no-fast-tanh") {
            fast_tanh = false;
        } else {
            usage();
            return a == "--help" || a == "-h" ? 0 : 2;
        }
    }
    if (frames == 0 || rate <= 0.0 || blocks == 0) {
        usage();
        return 2;
    }

    if (fast_tanh) {
        nam::activations::Activation::enable_fast_tanh();
    }
    enable_flush_to_zero(); // same MXCSR as the backends' audio threads

    TunerProcessor tuner;
    NoiseGateProcessor gate;
    CompressorProcessor comp;
    DynamicWahProcessor wah;
    FlangerProcessor flanger;
    PhaserProcessor phaser;
    NamProcessor nam;
    EQProcessor eq;
    ChorusProcessor chorus;
    TremoloProcessor tremolo;
    DelayProcessor delay;
    ReverbProcessor reverb;
    IRProcessor cab;
    DriveProcessor drive;
    DoublerProcessor doubler;
    VolumeProcessor volume;
    OutputLimiter limiter;
    TransposeProcessor transpose;
    transpose.set_semitones(-2.0f); // a drop tuning: the shifter's real cost

    // Same defaults as AudioEngine's constructor.
    chorus.set_stereo_mode(true);
    tremolo.set_stereo_mode(true);
    delay.set_stereo_mode(true);
    reverb.set_stereo_mode(true);

    std::vector<Stage> chain = {
        {"Tuner", &tuner, {}},     {"Transpose", &transpose, {}},
        {"Gate", &gate, {}},       {"Compressor", &comp, {}},
        {"Wah", &wah, {}},         {"Flanger", &flanger, {}},
        {"Phaser", &phaser, {}},   {"Drive", &drive, {}},
        {"Amp", &nam, {}},         {"EQ", &eq, {}},
        {"Doubler", &doubler, {}}, {"Chorus", &chorus, {}},
        {"Tremolo", &tremolo, {}}, {"Delay", &delay, {}},
        {"Reverb", &reverb, {}},   {"Cab", &cab, {}},
        {"Volume", &volume, {}},
    };

    for (auto& s : chain) {
        s.fx->set_sample_rate(rate);
        s.fx->set_enabled(listed(only, s.name));
    }
    nam.prepare_buffer(frames);
    if (!model.empty() && !nam.load_model(model)) {
        std::fprintf(stderr, "model: %s\n", nam.last_error().c_str());
        return 1;
    }
    if (!ir.empty() && !cab.load(ir)) {
        std::fprintf(stderr, "ir: %s\n", cab.last_error().c_str());
        return 1;
    }
    if (!ir_b.empty() && !cab.load_b(ir_b)) {
        std::fprintf(stderr, "ir-b: %s\n", cab.last_error_b().c_str());
        return 1;
    }
    nam.reclaim_retired(true);
    cab.reclaim_retired(true);
    if (model.empty()) {
        nam.set_enabled(false);
    }
    if (ir.empty()) {
        cab.set_enabled(false);
    }

    std::vector<float> l(frames), r(frames);
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> noise(-0.25f, 0.25f);

    const size_t warmup = std::max<size_t>(50, blocks / 20);
    limiter.set_sample_rate(rate);
    limiter.set_enabled(listed(only, "Limiter"));
    std::vector<double> limiter_us;
    limiter_us.reserve(blocks);
    bool finite = true;
    std::vector<double> total;
    total.reserve(blocks);
    for (auto& s : chain) {
        s.us.reserve(blocks);
    }

    using Clock = std::chrono::steady_clock;
    for (size_t b = 0; b < warmup + blocks; ++b) {
        for (size_t i = 0; i < frames; ++i) {
            l[i] = r[i] = noise(rng);
        }
        bool stereo = false;
        double block_us = 0.0;
        for (auto& s : chain) {
            const auto t0 = Clock::now();
            stereo = s.fx->process(l.data(), r.data(), frames, stereo);
            const double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
            block_us += us;
            if (b >= warmup) {
                s.us.push_back(us);
            }
        }
        {
            const auto t0 = Clock::now();
            limiter.process(l.data(), r.data(), frames);
            const double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
            block_us += us;
            if (b >= warmup) {
                limiter_us.push_back(us);
            }
        }
        for (size_t i = 0; i < frames; ++i) {
            if (!std::isfinite(l[i]) || !std::isfinite(r[i])) {
                finite = false;
            }
        }
        if (b >= warmup) {
            total.push_back(block_us);
        }
    }

    const double budget_us = static_cast<double>(frames) / rate * 1e6;
    std::printf(
        "NAMPlayerBench  %zu frames @ %.0f Hz  budget %.1f µs/block  %zu blocks  fast tanh %s\n",
        frames, rate, budget_us, blocks, fast_tanh ? "on" : "off");
    if (!model.empty()) {
        std::printf("model  %s\n", model.c_str());
    }
    if (!ir.empty()) {
        std::printf("ir     %s\n", ir.c_str());
    }
    std::printf("\n%-12s %10s %10s %10s %8s\n", "stage", "mean µs", "p99 µs", "max µs", "% budget");
    for (const auto& s : chain) {
        if (!s.fx->is_enabled()) {
            continue;
        }
        const double us = mean(s.us);
        std::printf("%-12s %10.2f %10.2f %10.2f %7.2f%%\n", s.name, us, percentile(s.us, 0.99),
                    percentile(s.us, 1.0), us / budget_us * 100.0);
    }
    if (limiter.is_enabled()) {
        const double us = mean(limiter_us);
        std::printf("%-12s %10.2f %10.2f %10.2f %7.2f%%\n", "Limiter", us,
                    percentile(limiter_us, 0.99), percentile(limiter_us, 1.0),
                    us / budget_us * 100.0);
    }
    const double total_us = mean(total);
    std::printf("%-12s %10.2f %10.2f %10.2f %7.2f%%\n", "TOTAL", total_us, percentile(total, 0.99),
                percentile(total, 1.0), total_us / budget_us * 100.0);
    if (!finite) {
        std::printf("\nWARNING: non-finite samples in the output\n");
    }
    return finite ? 0 : 1;
}
