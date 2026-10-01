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
    std::string model, ir, irB, only;
    size_t frames = 128;
    double rate = 48000.0;
    size_t blocks = 4000;
    bool fastTanh = true;

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
            irB = next();
        } else if (a == "--frames") {
            frames = std::strtoul(next(), nullptr, 10);
        } else if (a == "--rate") {
            rate = std::atof(next());
        } else if (a == "--blocks") {
            blocks = std::strtoul(next(), nullptr, 10);
        } else if (a == "--only") {
            only = next();
        } else if (a == "--no-fast-tanh") {
            fastTanh = false;
        } else {
            usage();
            return a == "--help" || a == "-h" ? 0 : 2;
        }
    }
    if (frames == 0 || rate <= 0.0 || blocks == 0) {
        usage();
        return 2;
    }

    if (fastTanh) {
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
    transpose.setSemitones(-2.0f); // a drop tuning: the shifter's real cost

    // Same defaults as AudioEngine's constructor.
    chorus.setStereoMode(true);
    tremolo.setStereoMode(true);
    delay.setStereoMode(true);
    reverb.setStereoMode(true);

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
        s.fx->setSampleRate(rate);
        s.fx->setEnabled(listed(only, s.name));
    }
    nam.prepareBuffer(frames);
    if (!model.empty() && !nam.loadModel(model)) {
        std::fprintf(stderr, "model: %s\n", nam.lastError().c_str());
        return 1;
    }
    if (!ir.empty() && !cab.load(ir)) {
        std::fprintf(stderr, "ir: %s\n", cab.lastError().c_str());
        return 1;
    }
    if (!irB.empty() && !cab.loadB(irB)) {
        std::fprintf(stderr, "ir-b: %s\n", cab.lastErrorB().c_str());
        return 1;
    }
    nam.reclaimRetired(true);
    cab.reclaimRetired(true);
    if (model.empty()) {
        nam.setEnabled(false);
    }
    if (ir.empty()) {
        cab.setEnabled(false);
    }

    std::vector<float> L(frames), R(frames);
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> noise(-0.25f, 0.25f);

    const size_t warmup = std::max<size_t>(50, blocks / 20);
    limiter.setSampleRate(rate);
    limiter.setEnabled(listed(only, "Limiter"));
    std::vector<double> limiterUs;
    limiterUs.reserve(blocks);
    bool finite = true;
    std::vector<double> total;
    total.reserve(blocks);
    for (auto& s : chain) {
        s.us.reserve(blocks);
    }

    using clock = std::chrono::steady_clock;
    for (size_t b = 0; b < warmup + blocks; ++b) {
        for (size_t i = 0; i < frames; ++i) {
            L[i] = R[i] = noise(rng);
        }
        bool stereo = false;
        double blockUs = 0.0;
        for (auto& s : chain) {
            const auto t0 = clock::now();
            stereo = s.fx->process(L.data(), R.data(), frames, stereo);
            const double us = std::chrono::duration<double, std::micro>(clock::now() - t0).count();
            blockUs += us;
            if (b >= warmup) {
                s.us.push_back(us);
            }
        }
        {
            const auto t0 = clock::now();
            limiter.process(L.data(), R.data(), frames);
            const double us = std::chrono::duration<double, std::micro>(clock::now() - t0).count();
            blockUs += us;
            if (b >= warmup) {
                limiterUs.push_back(us);
            }
        }
        for (size_t i = 0; i < frames; ++i) {
            if (!std::isfinite(L[i]) || !std::isfinite(R[i])) {
                finite = false;
            }
        }
        if (b >= warmup) {
            total.push_back(blockUs);
        }
    }

    const double budgetUs = static_cast<double>(frames) / rate * 1e6;
    std::printf(
        "NAMPlayerBench  %zu frames @ %.0f Hz  budget %.1f µs/block  %zu blocks  fast tanh %s\n",
        frames, rate, budgetUs, blocks, fastTanh ? "on" : "off");
    if (!model.empty()) {
        std::printf("model  %s\n", model.c_str());
    }
    if (!ir.empty()) {
        std::printf("ir     %s\n", ir.c_str());
    }
    std::printf("\n%-12s %10s %10s %10s %8s\n", "stage", "mean µs", "p99 µs", "max µs", "% budget");
    for (const auto& s : chain) {
        if (!s.fx->isEnabled()) {
            continue;
        }
        const double us = mean(s.us);
        std::printf("%-12s %10.2f %10.2f %10.2f %7.2f%%\n", s.name, us, percentile(s.us, 0.99),
                    percentile(s.us, 1.0), us / budgetUs * 100.0);
    }
    if (limiter.isEnabled()) {
        const double us = mean(limiterUs);
        std::printf("%-12s %10.2f %10.2f %10.2f %7.2f%%\n", "Limiter", us,
                    percentile(limiterUs, 0.99), percentile(limiterUs, 1.0), us / budgetUs * 100.0);
    }
    const double totalUs = mean(total);
    std::printf("%-12s %10.2f %10.2f %10.2f %7.2f%%\n", "TOTAL", totalUs, percentile(total, 0.99),
                percentile(total, 1.0), totalUs / budgetUs * 100.0);
    if (!finite) {
        std::printf("\nWARNING: non-finite samples in the output\n");
    }
    return finite ? 0 : 1;
}
