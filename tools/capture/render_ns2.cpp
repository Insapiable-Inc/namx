// NAM-X's Noise Gate (the NS-2 model, NoiseGateProcessor) rendered offline: a capture's played
// signal through the processor exactly as the app runs it — for checking it against the pedal
// (tools/capture/ns2/reamp_ab.py).
//
//   render_ns2 OPENS_AT_DBFS DECAY_MS < mono.f32 > mono.f32
//
// OPENS_AT_DBFS: the Threshold knob (where the gate opens, dBFS re NAM-X's −6 dBFS guitar peak);
// DECAY_MS: the Release knob (the pedal's Decay). Input and output: raw float32 mono at 48 kHz in
// NAM-X's scale, processed in 256-frame blocks. Build from the repository root:
//
//   g++ -std=c++23 -O2 -Isrc/dsp tools/capture/render_ns2.cpp src/dsp/NoiseGateProcessor.cpp -o render_ns2
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "NoiseGateProcessor.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: render_ns2 OPENS_AT_DBFS DECAY_MS < mono.f32 > mono.f32\n");
        return 2;
    }
    namx::NoiseGateProcessor gate;
    gate.set_sample_rate(48000.0);
    gate.set_enabled(true);
    gate.set_threshold(std::strtof(argv[1], nullptr));
    gate.set_release(std::strtof(argv[2], nullptr));
    std::vector<float> in;
    float buf[4096];
    size_t got;
    while ((got = std::fread(buf, sizeof(float), 4096, stdin)) > 0) {
        in.insert(in.end(), buf, buf + got);
    }
    std::vector<float> right(256);
    for (size_t done = 0; done < in.size(); done += 256) {
        const size_t n = std::min<size_t>(256, in.size() - done);
        std::copy(in.data() + done, in.data() + done + n, right.data());
        gate.process(in.data() + done, right.data(), n, false);
    }
    std::fwrite(in.data(), sizeof(float), in.size(), stdout);
    return 0;
}
