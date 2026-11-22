// NAM-X's Compressor (the CS-3 models, CompressorProcessor) rendered offline: a capture's played
// signal through the processor exactly as the app runs it, mono, for checking it against the
// pedal (tools/capture/cs3/focused.py).
//
//   render_cs3 THRESHOLD_DBFS RELEASE_MS TONE_PCT < mono.f32 > mono.f32
//
// The knobs as NAM-X has them: Threshold in dBFS, Release in ms (the recovery's time scale: the
// pedal's Attack 7:00, 12:00, 5:00 = 1152, 300, 99.5), Tone 0–100 % (50 = the pedal's 12:00); Level at 0 dB (the pedal at full Level). Input and
// output: raw float32 mono at 48 kHz in NAM-X's scale, 256-frame blocks. Build from the
// repository root:
//
//   g++ -std=c++23 -O2 -Isrc/dsp tools/capture/render_cs3.cpp src/dsp/CompressorProcessor.cpp -o build-cs3/render_cs3
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "CompressorProcessor.h"

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(
            stderr, "usage: render_cs3 THRESHOLD_DBFS RELEASE_MS TONE_PCT < mono.f32 > mono.f32\n");
        return 2;
    }
    namx::CompressorProcessor compressor;
    compressor.set_threshold(std::strtof(argv[1], nullptr));
    compressor.set_release(std::strtof(argv[2], nullptr));
    compressor.set_tone_pct(std::strtof(argv[3], nullptr));
    compressor.set_makeup(0.0f);
    compressor.set_sample_rate(48000.0);
    compressor.set_enabled(true);
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
        compressor.process(in.data() + done, right.data(), n, false);
    }
    std::fwrite(in.data(), sizeof(float), in.size(), stdout);
    return 0;
}
