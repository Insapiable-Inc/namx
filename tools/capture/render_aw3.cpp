// NAM-X's Wah (the AW-3 model, DynamicWahProcessor) rendered offline: a capture's played signal
// through the processor exactly as the app runs it, mono, for checking it against the pedal
// (tools/capture/aw3/focused.py).
//
//   render_aw3 TYPE MANUAL_PCT SENS_PCT DECAY_PCT < mono.f32 > mono.f32
//
// TYPE 1–3 = Up, Down, Sharp; the knobs 0–100 % = the pedal's 7:00–5:00; Mix at 100 %. Input
// and output: raw float32 mono at 48 kHz in NAM-X's scale, 256-frame blocks. Build from the
// repository root:
//
//   g++ -std=c++23 -O2 -Isrc/dsp tools/capture/render_aw3.cpp src/dsp/DynamicWahProcessor.cpp -o build-aw3/render_aw3
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "DynamicWahProcessor.h"

int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(
            stderr,
            "usage: render_aw3 TYPE MANUAL_PCT SENS_PCT DECAY_PCT < mono.f32 > mono.f32\n");
        return 2;
    }
    namx::DynamicWahProcessor wah;
    wah.set_type(std::atoi(argv[1]));
    wah.set_position_pct(std::strtof(argv[2], nullptr));
    wah.set_sensitivity_pct(std::strtof(argv[3], nullptr));
    wah.set_decay_pct(std::strtof(argv[4], nullptr));
    wah.set_mix_pct(100.0f);
    wah.set_sample_rate(48000.0);
    wah.set_enabled(true);
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
        wah.process(in.data() + done, right.data(), n, false);
    }
    std::fwrite(in.data(), sizeof(float), in.size(), stdout);
    return 0;
}
