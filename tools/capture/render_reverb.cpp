// NAM-X's Reverb rendered offline, for comparing it with a measured pedal (analyze_rv6.py,
// fit_rv6.py).
//
//   render_reverb TYPE DECAY_PCT DAMPING_PCT PRE_DELAY_MS SHIMMER_PCT [MIX_PCT]
//       < mono.f32 > stereo.f32
//
// TYPE: 0 Plate, 1 Hall, 2 Room; 3-8 the RV-6's Modulate, Spring, Plate, Hall, Room,
// Shimmer (DECAY = Time, DAMPING = Tone, MIX = Level). Input: raw float32 mono at 48 kHz;
// output: raw float32 stereo (L, R interleaved), processed in 256-frame blocks like the
// app, Width 100 %, cuts open. The classic types at Mix 100 % (the default) give the wet
// only; the RV-6 types always keep the dry at unity (subtract the input for their wet).
// Build from the repository root (one command):
//
//   g++ -std=c++23 -O2 -Isrc/dsp -Iexternal/signalsmith-stretch/include
//       -Iexternal/signalsmith-linear/include tools/capture/render_reverb.cpp
//       src/dsp/ReverbProcessor.cpp -o render_reverb
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "ReverbProcessor.h"

int main(int argc, char** argv) {
    if (argc != 6 && argc != 7) {
        std::fprintf(stderr, "usage: render_reverb TYPE DECAY_PCT DAMPING_PCT PRE_DELAY_MS "
                             "SHIMMER_PCT [MIX_PCT]\n");
        return 2;
    }
    std::vector<float> input;
    float chunk[4096];
    size_t count = 0;
    while ((count = std::fread(chunk, sizeof(float), 4096, stdin)) > 0) {
        input.insert(input.end(), chunk, chunk + count);
    }
    namx::ReverbProcessor reverb;
    reverb.set_sample_rate(48000.0);
    reverb.set_enabled(true);
    reverb.set_stereo_mode(true);
    reverb.set_type(std::atoi(argv[1]));
    reverb.set_room_size_pct(static_cast<float>(std::atof(argv[2])));
    reverb.set_damping_pct(static_cast<float>(std::atof(argv[3])));
    reverb.set_pre_delay(static_cast<float>(std::atof(argv[4])));
    reverb.set_shimmer_pct(static_cast<float>(std::atof(argv[5])));
    reverb.set_mix_pct(argc == 7 ? static_cast<float>(std::atof(argv[6])) : 100.0f);
    reverb.set_width_pct(100.0f);
    reverb.set_low_cut(20.0f);
    reverb.set_high_cut(20000.0f);
    reverb.reset();
    constexpr size_t BLOCK = 256;
    std::vector<float> left(BLOCK), right(BLOCK), output;
    output.reserve(2 * input.size());
    for (size_t start = 0; start < input.size(); start += BLOCK) {
        const size_t frames = std::min(BLOCK, input.size() - start);
        std::copy_n(input.begin() + static_cast<long>(start), frames, left.begin());
        std::copy_n(input.begin() + static_cast<long>(start), frames, right.begin());
        reverb.process(left.data(), right.data(), frames, false);
        for (size_t i = 0; i < frames; ++i) {
            output.push_back(left[i]);
            output.push_back(right[i]);
        }
    }
    std::fwrite(output.data(), sizeof(float), output.size(), stdout);
}
