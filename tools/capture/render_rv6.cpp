// Render the same RV-6 engine and embedded Shimmer bank as the rack.
//
//   render_rv6 MODE TIME TONE LEVEL [NAME=VALUE ...] < mono.f32 > stereo.f32
//
// MODE 0-5: Modulate, Spring, Plate, Hall, Room, Shimmer. TIME, TONE, LEVEL: 0-1 of the
// knob's turn (7:00 to 5:00). Input: raw float32 mono at 48 kHz; output: raw float32 stereo,
// the dry plus outputs A and B as the Reverb plays them (256-frame blocks).
// Tank modes: rt60=v,v,v,v,v energy=v,v,v,v,v early_db=v,v,v,v,v early_from=MS (the
// mode's Time law: the RT60 at 1 kHz, the wet's energy correction and the measured start's
// level at the five positions, and where the start's extra decay begins); early=0 leaves the
// measured early responses (Room, Hall, Plate, Spring) out: the tanks alone; rate=HZ runs the
// engine (input and output) at another rate; decay.FIELD= (low_scale, high_scale, mid_hz,
// mid_q, mid_ratio, slow_rt, slow_gain, slow_low_scale, slow_high_scale; slow_at=v,v,v,v,v)
// another DecayShape; spring.FIELD= (low_cut_hz, high_cut_hz, allpass, transition_hz) another
// spring; low.gain_db= low.rt60=v,v,v,v,v low.time_db=v,v,v,v,v (this mode's)
// low.at=v,v,v,v,v low.band=KIND,HZ,Q,DB[,KIND,HZ,Q,DB] (KIND as FilterKind's number) another
// low reverb for Tone below noon.
// Build from the repository root:
//
//   cmake --build build-release --target render_rv6
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "ReverbProcessor.h"

namespace
{

std::vector<float> values(const char* text) {
    std::vector<float> out;
    char* end = nullptr;
    for (const char* p = text; *p != '\0'; p = (*end == ',') ? end + 1 : end) {
        out.push_back(std::strtof(p, &end));
        if (end == p) {
            break;
        }
    }
    return out;
}

bool table(const std::vector<float>& v, namx::array<float, 5>& out) {
    if (v.size() != out.size()) {
        return false;
    }
    std::copy(v.begin(), v.end(), out.begin());
    return true;
}

} // namespace

int main(int argc, char** argv) {
    using namespace namx::rv6;
    if (argc < 5) {
        std::fprintf(stderr, "usage: render_rv6 MODE TIME TONE LEVEL [NAME=VALUE ...]\n");
        return 2;
    }
    bool early = true;
    double rate = 48000.0;
    const int number = std::atoi(argv[1]);
    if (number < 0 || number >= static_cast<int>(MODE_COUNT)) {
        return 2;
    }
    const auto mode = static_cast<Mode>(number);
    Voicing voicing = VOICINGS[static_cast<size_t>(mode)];
    namx::array<float, 5> early_db = EARLY[static_cast<size_t>(mode)].gain_db;
    float early_from_ms = EARLY[static_cast<size_t>(mode)].decay_from_ms;
    bool time_law_set = false;
    LowShape low = LOW;
    bool low_set = false;
    DecayShape decay = DECAYS[static_cast<size_t>(mode)];
    bool decay_set = false;
    SpringShape spring = SPRING;
    bool spring_set = false;
    for (int i = 5; i < argc; ++i) {
        const std::string arg = argv[i];
        const size_t eq = arg.find('=');
        if (eq == std::string::npos) {
            std::fprintf(stderr, "bad override: %s\n", argv[i]);
            return 2;
        }
        const std::string name = arg.substr(0, eq);
        const std::vector<float> v = values(argv[i] + eq + 1);
        if (mode == Mode::SHIMMER && name != "rate") {
            std::fprintf(
                stderr,
                "Shimmer is calibrated by rv6/shimmer_prototype.py and rv6/export_shimmer.py\n");
            return 2;
        }
        bool ok = !v.empty();
        if (name == "rt60") {
            ok = ok && table(v, voicing.rt60_1k);
            time_law_set = true;
        } else if (name == "energy") {
            ok = ok && table(v, voicing.energy_db);
            time_law_set = true;
        } else if (name == "early_db") {
            ok = ok && table(v, early_db);
            time_law_set = true;
        } else if (name == "early_from") {
            early_from_ms = v[0];
            time_law_set = true;
        } else if (name == "low.gain_db") {
            low.gain_db[static_cast<size_t>(mode)] = v[0];
            low_set = true;
        } else if (name == "low.at") {
            ok = ok && table(v, low.at);
            low_set = true;
        } else if (name == "low.time_db") {
            ok = ok && table(v, low.time_db[static_cast<size_t>(mode)]);
            low_set = true;
        } else if (name == "low.rt60") {
            ok = ok && table(v, low.rt60[static_cast<size_t>(mode)]);
            low_set = true;
        } else if (name == "low.band") {
            ok = ok && v.size() % 4 == 0 && v.size() / 4 <= low.band.size();
            if (ok) {
                low.band = {};
                for (size_t k = 0; k < v.size() / 4; ++k) {
                    low.band[k] = {static_cast<namx::FilterKind>(static_cast<int>(v[4 * k])),
                                   v[4 * k + 1], v[4 * k + 2], v[4 * k + 3]};
                }
                low_set = true;
            }
        } else if (name == "early") {
            early = v[0] != 0.0f;
        } else if (name == "rate") {
            rate = v[0];
        } else if (name.starts_with("spring.")) {
            const std::string field = name.substr(7);
            float* const target = field == "low_cut_hz"      ? &spring.low_cut_hz
                                  : field == "high_cut_hz"   ? &spring.high_cut_hz
                                  : field == "allpass"       ? &spring.allpass
                                  : field == "transition_hz" ? &spring.transition_hz
                                                             : nullptr;
            ok = ok && target != nullptr;
            if (ok) {
                *target = v[0];
                spring_set = true;
            }
        } else if (name == "decay.slow_at") {
            ok = ok && table(v, decay.slow_at);
            decay_set = decay_set || ok;
        } else if (name.starts_with("decay.")) {
            const std::string field = name.substr(6);
            float* const target = field == "low_scale"         ? &decay.low_scale
                                  : field == "high_scale"      ? &decay.high_scale
                                  : field == "mid_hz"          ? &decay.mid_hz
                                  : field == "mid_q"           ? &decay.mid_q
                                  : field == "mid_ratio"       ? &decay.mid_ratio
                                  : field == "slow_rt"         ? &decay.slow_rt
                                  : field == "slow_gain"       ? &decay.slow_gain
                                  : field == "slow_low_scale"  ? &decay.slow_low_scale
                                  : field == "slow_high_scale" ? &decay.slow_high_scale
                                                               : nullptr;
            ok = ok && target != nullptr;
            if (ok) {
                *target = v[0];
                decay_set = true;
            }
        } else {
            ok = false;
        }
        if (!ok) {
            std::fprintf(stderr, "bad override: %s\n", argv[i]);
            return 2;
        }
    }
    if (!std::isfinite(rate) || rate <= 0) {
        return 2;
    }
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::vector<float> input;
    float chunk[4096];
    size_t count = 0;
    while ((count = std::fread(chunk, sizeof(float), 4096, stdin)) > 0) {
        input.insert(input.end(), chunk, chunk + count);
    }
    const float time = std::strtof(argv[2], nullptr);
    const float tone = std::strtof(argv[3], nullptr);
    const float level = std::strtof(argv[4], nullptr);
    constexpr size_t BLOCK = 256;
    if (!time_law_set && !low_set && !decay_set && !spring_set && early) {
        // The normal comparison enters through the rack, including the mono-to-stereo
        // dispatch, preset type, three panel knobs, dry path and sample-rate bridge.
        namx::ReverbProcessor rack;
        rack.set_enabled(true);
        rack.set_stereo_mode(true);
        rack.set_type(namx::ReverbProcessor::FIRST_TYPE + number);
        rack.set_sample_rate(rate);
        rack.set_room_size_pct(100 * time);
        rack.set_damping_pct(100 * tone);
        rack.set_mix_pct(100 * level);
        std::array<float, BLOCK> right{};
        std::array<float, 2 * BLOCK> interleaved{};
        for (size_t start = 0; start < input.size(); start += BLOCK) {
            const size_t frames = std::min(BLOCK, input.size() - start);
            float* left = input.data() + start;
            rack.process(left, right.data(), frames, false);
            for (size_t i = 0; i < frames; ++i) {
                interleaved[2 * i] = left[i];
                interleaved[2 * i + 1] = right[i];
            }
            std::fwrite(interleaved.data(), sizeof(float), 2 * frames, stdout);
        }
        return 0;
    }
    Engine engine;
    engine.prepare(rate);
    if (time_law_set) {
        engine.set_time_law(mode, voicing.rt60_1k, voicing.energy_db, early_db, early_from_ms);
    }
    if (low_set) {
        engine.set_low_shape(low);
    }
    if (decay_set) {
        engine.set_decay_shape(mode, decay);
    }
    if (spring_set) {
        engine.set_spring_shape(spring);
    }
    engine.set_early_enabled(early);
    engine.set_controls(mode, time, tone, level);
    engine.reset();
    std::vector<float> output;
    output.reserve(2 * input.size());
    for (size_t start = 0; start < input.size(); start += BLOCK) {
        engine.set_controls(mode, time, tone, level);
        const size_t frames = std::min(BLOCK, input.size() - start);
        for (size_t i = start; i < start + frames; ++i) {
            float a = 0.0f, b = 0.0f;
            engine.process(input[i], a, b);
            output.push_back(input[i] + a);
            output.push_back(input[i] + b);
        }
    }
    std::fwrite(output.data(), sizeof(float), output.size(), stdout);
    return 0;
}
