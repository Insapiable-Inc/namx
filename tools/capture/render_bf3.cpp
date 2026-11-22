// NAM-X's Flanger (the BF-3 models, FlangerProcessor) rendered offline: a capture's played
// signal through the processor exactly as the app runs it, mono, for checking it against the
// pedal (tools/capture/bf3/focused.py).
//
//   render_bf3 TYPE MANUAL RES DEPTH RATE_HZ < mono.f32 > mono.f32
//
// TYPE 1–2: Standard, Ultra. MANUAL, RES, DEPTH: the pedal's knobs, 0–1 of their rotation
// (7:00–5:00), turned into NAM-X's Manual (ms), Feedback (%) and Depth (%). RATE_HZ: the sweep's rate as measured on the take (NAM-X's Rate
// is in Hz). The sweep starts at its bottom (the shortest delay) with the first sample: put
// silence before the signal to place it. Input and output: raw float32 mono at 48 kHz in
// NAM-X's scale, 256-frame blocks. Build from the repository root:
//
//   g++ -std=c++23 -O2 -Isrc/dsp tools/capture/render_bf3.cpp src/dsp/FlangerProcessor.cpp -o build-bf3/render_bf3
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "Constants.h"
#include "FlangerProcessor.h"

int main(int argc, char** argv) {
    if (argc != 6) {
        std::fprintf(stderr, "usage: render_bf3 TYPE MANUAL RES DEPTH RATE_HZ < mono.f32 > mono.f32\n");
        return 2;
    }
    namx::FlangerProcessor flanger;
    flanger.set_type(std::atoi(argv[1]));
    flanger.set_manual_ms(namx::bf3::manual_ms_from_pedal(std::strtof(argv[2], nullptr)));
    flanger.set_feedback_pct(100.0f * namx::bf3::feedback_from_pedal(std::strtof(argv[3], nullptr)));
    flanger.set_depth_pct(100.0f * namx::bf3::depth_from_pedal(std::strtof(argv[4], nullptr)));
    flanger.set_rate_hz(std::strtof(argv[5], nullptr));
    flanger.set_sample_rate(48000.0);
    flanger.set_enabled(true);
    std::vector<float> in;
    float buf[4096];
    size_t got;
    while ((got = std::fread(buf, sizeof(float), 4096, stdin)) > 0) {
        in.insert(in.end(), buf, buf + got);
    }
    std::vector<float> right(in);
    for (size_t done = 0; done < in.size(); done += 256) {
        const size_t n = std::min<size_t>(256, in.size() - done);
        flanger.process(in.data() + done, right.data() + done, n, false);
    }
    std::fwrite(in.data(), sizeof(float), in.size(), stdout);
    return 0;
}
