// NAM-X's Tremolo (the TR-2 model, TremoloProcessor) rendered offline: a capture's played signal
// through the processor exactly as the app runs it, mono, for checking it against the pedal
// (tools/capture/tr2/focused.py).
//
//   render_tr2 RATE_HZ DEPTH_POSITION WAVE_POSITION < mono.f32 > mono.f32
//
// Rate in Hz; Depth and Wave as set on the pedal for a capture, 0–100 % of the knob's travel
// (7:00–5:00), given to the processor as NAM-X's own knobs (tr2::depth_from_pedal,
// wave_from_pedal: the same sound; NAM-X's knobs are plain curves, not the pot's). The LFO starts
// at phase 0 with the first sample: pad the input's start to place it. Input and output: raw
// float32 mono at 48 kHz, processed in 256-frame blocks. Build from the repository root:
//
//   g++ -std=c++23 -O2 -Isrc/dsp tools/capture/render_tr2.cpp src/dsp/TremoloProcessor.cpp -o build-tr2/render_tr2
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "TremoloProcessor.h"

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr,
                     "usage: render_tr2 RATE_HZ DEPTH_POSITION WAVE_POSITION < mono.f32 > mono.f32\n");
        return 2;
    }
    namx::TremoloProcessor tremolo;
    tremolo.set_sample_rate(48000.0);
    tremolo.set_enabled(true);
    tremolo.set_stereo_mode(false);
    const float rate = std::strtof(argv[1], nullptr);
    const namx::tr2::Voicing& m = namx::tr2::SUPPLY;
    tremolo.set_rate_hz(rate);
    tremolo.set_depth_pct(100.0f * namx::tr2::depth_from_pedal(m, std::strtof(argv[2], nullptr) / 100.0f));
    tremolo.set_wave_pct(100.0f *
                         namx::tr2::wave_from_pedal(m, std::strtof(argv[3], nullptr) / 100.0f, rate));
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
        tremolo.process(in.data() + done, right.data(), n, false);
    }
    std::fwrite(in.data(), sizeof(float), in.size(), stdout);
    return 0;
}
