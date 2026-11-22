// The RT-2 model (rt2::Engine) rendered offline: a capture's played signal through the engine,
// the rotors started where the capture's were — for checking the engine against the fit and
// the pedal (fit_rt2.py).
//
//   render_rt2 MODE HORN_HZ DRUM_HZ HORN_TURNS DRUM_TURNS [NAME=VALUE ...] < mono.f32 > stereo.f32
//
// MODE 0-2: Mode I, II, III. The rotors turn at HORN_HZ and DRUM_HZ from HORN_TURNS and
// DRUM_TURNS at the first input sample (the speed knobs' targets are the same speeds, so they
// hold). Input: raw float32 mono at 48 kHz in NAM-X's scale (the guitar's peak at −6 dBFS);
// output: raw float32 stereo, Outputs A and B, 256-frame blocks. Overrides: drive= (0–1 of the
// knob), horn= drum= (Balance's gains, linear), level= (linear), rate=HZ (input and output at
// another rate), fast=1 (the Fast switch on: HORN_HZ is the fast speed, the drum's fast ratio).
// Build from the repository root:
//
//   g++ -std=c++23 -O2 -Isrc/dsp tools/capture/render_rt2.cpp src/dsp/Rt2Rotary.cpp -o render_rt2
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Rt2Rotary.h"

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr, "usage: render_rt2 MODE HORN_HZ DRUM_HZ HORN_TURNS DRUM_TURNS [NAME=VALUE ...]\n");
        return 2;
    }
    namx::rt2::Settings s;
    s.mode = std::atoi(argv[1]);
    const float horn_hz = std::strtof(argv[2], nullptr);
    const float drum_hz = std::strtof(argv[3], nullptr);
    s.slow_hz = horn_hz;
    s.fast = false;
    double rate = 48000.0;
    for (int i = 6; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto eq = arg.find('=');
        const std::string name = arg.substr(0, eq);
        const float v = std::strtof(arg.c_str() + eq + 1, nullptr);
        if (name == "drive") {
            s.drive = v;
        } else if (name == "horn") {
            s.horn_gain = v;
        } else if (name == "drum") {
            s.drum_gain = v;
        } else if (name == "level") {
            s.level = v;
        } else if (name == "rate") {
            rate = v;
        } else if (name == "fast") {
            s.fast = v != 0.0f;
        }
    }
    namx::rt2::Engine engine;
    engine.prepare(rate);
    engine.set_rotors(std::strtod(argv[4], nullptr), std::strtod(argv[5], nullptr), horn_hz, drum_hz);
    // The speed knobs' targets are the given speeds, so the rotors hold them.
    s.slow_hz = horn_hz;
    s.fast_hz = horn_hz;
    std::vector<float> in;
    float buf[4096];
    size_t got;
    while ((got = std::fread(buf, sizeof(float), 4096, stdin)) > 0) {
        in.insert(in.end(), buf, buf + got);
    }
    std::vector<float> a(in.size()), b(in.size()), out(2 * in.size());
    for (size_t done = 0; done < in.size(); done += 256) {
        const size_t n = std::min<size_t>(256, in.size() - done);
        engine.process(in.data() + done, a.data() + done, b.data() + done, n, s);
    }
    for (size_t i = 0; i < in.size(); ++i) {
        out[2 * i] = a[i];
        out[2 * i + 1] = b[i];
    }
    std::fwrite(out.data(), sizeof(float), out.size(), stdout);
    (void)drum_hz;
    return 0;
}
