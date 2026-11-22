// NAM-X's Phaser (the PH-3 models, PhaserProcessor) rendered offline: a capture's played
// signal through the processor exactly as the app runs it, mono, for checking it against the
// pedal (tools/capture/ph3/focused.py).
//
//   render_ph3 TYPE RATE_HZ DEPTH RES < mono.f32 > mono.f32
//
// TYPE 1–7: 4, 8, 10, 12 Stage, Rise, Fall, Step. DEPTH, RES: the pedal's knobs, 0–1 of their
// rotation (7:00–5:00). RATE_HZ: the sweep's rate as measured on the take (NAM-X's Rate is in
// Hz). The sweep starts at phase 0 with the first sample: put silence before the signal to
// place it. Input and output: raw float32 mono at 48 kHz in NAM-X's scale, 256-frame blocks.
// Build from the repository root:
//
//   g++ -std=c++23 -O2 -Isrc/dsp tools/capture/render_ph3.cpp src/dsp/PhaserProcessor.cpp -o build-ph3/render_ph3
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "Constants.h"
#include "PhaserProcessor.h"

int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(stderr, "usage: render_ph3 TYPE RATE_HZ DEPTH RES < mono.f32 > mono.f32\n");
        return 2;
    }
    namx::PhaserProcessor phaser;
    phaser.set_type(std::atoi(argv[1]));
    phaser.set_rate_hz(std::strtof(argv[2], nullptr));
    // NAM-X's knobs are the travel and the loop's gain: the values a pedal position stands for.
    const namx::ph3::Voicing& v =
        namx::ph3::voicing(std::clamp(std::atoi(argv[1]), 1, 7) - 1);
    phaser.set_depth_pct(100.0f * namx::ph3::depth_from_pedal(std::strtof(argv[3], nullptr)));
    phaser.set_feedback_pct(100.0f *
                            namx::ph3::feedback_from_pedal(v, std::strtof(argv[4], nullptr)));
    phaser.set_sample_rate(48000.0);
    phaser.set_enabled(true);
    std::vector<float> in;
    float buf[4096];
    size_t got;
    while ((got = std::fread(buf, sizeof(float), 4096, stdin)) > 0) {
        in.insert(in.end(), buf, buf + got);
    }
    std::vector<float> right(in);
    for (size_t done = 0; done < in.size(); done += 256) {
        const size_t n = std::min<size_t>(256, in.size() - done);
        phaser.process(in.data() + done, right.data() + done, n, false);
    }
    std::fwrite(in.data(), sizeof(float), in.size(), stdout);
    return 0;
}
