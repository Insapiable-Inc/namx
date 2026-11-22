// The RT-2's Drive alone (rt2::Drive) rendered offline, for checking the C++ against the
// fitted model (tools/capture/rt2/drive3_check.py, drive12.py).
//
//   render_rt2_drive MODE KNOB [RATE] < mono.f32 > mono.f32
//
// MODE 0-2: Mode I, II, III. KNOB 0-1 (7:00-5:00). Input and output: raw float32 mono, 1.0 = the guitar's peak. Build
// from the repository root:
//
//   g++ -std=c++23 -O2 -Isrc/dsp tools/capture/render_rt2_drive.cpp -o render_rt2_drive
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "Rt2Drive.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: render_rt2_drive MODE KNOB [RATE] < mono.f32 > mono.f32\n");
        return 2;
    }
    namx::rt2::Drive drive;
    drive.prepare(argc > 3 ? std::strtod(argv[3], nullptr) : 48000.0);
    drive.set(std::atoi(argv[1]), std::strtof(argv[2], nullptr));
    std::vector<float> buf(4096);
    size_t got;
    while ((got = std::fread(buf.data(), sizeof(float), buf.size(), stdin)) > 0) {
        for (size_t i = 0; i < got; ++i) {
            buf[i] = drive.process(buf[i]);
        }
        std::fwrite(buf.data(), sizeof(float), got, stdout);
    }
    return 0;
}
