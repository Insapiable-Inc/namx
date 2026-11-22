// NAM-X's Equalizer (the GE-7 model, EQProcessor) rendered offline: a capture's played signal
// through the processor exactly as the app runs it, for checking it against the pedal
// (tools/capture/ge7/focused.py).
//
//   render_ge7 HZ100 HZ200 HZ400 HZ800 HZ1600 HZ3200 HZ6400 LEVEL < mono.f32 > mono.f32
//
// The eight sliders on the pedal's printed scale (−15…+15), as set on the pedal for a capture: each
// is given to the model as the dB its mark gives (ge7::printed_mark_db), the bands at the pedal's
// frequencies; the Low and High Cut stay off. Input
// and output: raw float32 mono at 48 kHz in NAM-X's scale, processed in 256-frame blocks. Build
// from the repository root:
//
//   g++ -std=c++23 -O2 -Isrc/dsp tools/capture/render_ge7.cpp src/dsp/EQProcessor.cpp -o build-ge7/render_ge7
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "EQProcessor.h"

int main(int argc, char** argv) {
    if (argc != 9) {
        std::fprintf(stderr, "usage: render_ge7 HZ100 … HZ6400 LEVEL < mono.f32 > mono.f32\n");
        return 2;
    }
    namx::EQProcessor eq;
    eq.set_sample_rate(48000.0);
    eq.set_enabled(true);
    for (size_t slider = 0; slider < 8; ++slider) {
        eq.set_slider(slider, namx::ge7::printed_mark_db(slider, std::strtod(argv[slider + 1], nullptr)));
    }
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
        eq.process(in.data() + done, right.data(), n, false);
    }
    std::fwrite(in.data(), sizeof(float), in.size(), stdout);
    return 0;
}
