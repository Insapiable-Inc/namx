// The NS-2 model's control circuit alone (ns2::Control, Ns2Gate.h): the gain it gives, per sample, for
// a signal — for checking the C++ against the fitted model (tools/capture/ns2/circuit.py engine).
//
//   render_ns2_gain OPENS_AT_DBFS DECAY_MS [RATE] < mono.f32 > gain.f32
//
// Input: raw float32 mono in NAM-X's scale (the guitar's peak at −6 dBFS). Build from the repository
// root:
//
//   g++ -std=c++23 -O2 -Isrc/dsp tools/capture/render_ns2_gain.cpp -o build-ns2/render_ns2_gain
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "Ns2Gate.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: render_ns2_gain OPENS_AT_DBFS DECAY_MS [RATE] < mono.f32 > gain.f32\n");
        return 2;
    }
    namx::ns2::Control control;
    control.prepare(argc > 3 ? std::strtod(argv[3], nullptr) : 48000.0);
    control.set(std::strtof(argv[1], nullptr), std::strtof(argv[2], nullptr));
    const float reference = std::pow(10.0f, namx::ns2::REFERENCE_DBFS / 20.0f);
    std::vector<float> buf(4096);
    size_t got;
    while ((got = std::fread(buf.data(), sizeof(float), buf.size(), stdin)) > 0) {
        for (size_t i = 0; i < got; ++i) {
            buf[i] = control.process(buf[i] / reference);
        }
        std::fwrite(buf.data(), sizeof(float), got, stdout);
    }
    return 0;
}
