// NAM-X's Chorus (the CE-5 model, ChorusProcessor) rendered offline: a capture's played signal
// through the processor exactly as the app runs it, mono, for checking it against the pedal
// (tools/capture/ce5/focused.py).
//
//   render_ce5 ELEVEL RATE_HZ DEPTH LOW HIGH < mono.f32 > two.f32
//
// ELEVEL, DEPTH, LOW, HIGH: the pedal's knobs, 0–1 of their rotation (7:00–5:00); they are
// turned into NAM-X's Level (dB), Depth (%) and Low and High Cut (Hz). RATE_HZ: the sweep's rate as measured
// on the take (NAM-X's Rate is in Hz). The sweep starts at its top with the first sample: put
// silence before the signal to place it. Input: raw float32 mono at 48 kHz in NAM-X's scale;
// output interleaved: the chorus alone (the mono output minus the dry: the pedal's OUTPUT A
// with a plug in B), then the dry (the input through the model's input clip and dry gain: its
// OUTPUT B); their sum is the mono output. 256-frame blocks. Build from the repository root:
//
//   g++ -std=c++23 -O2 -Isrc/dsp tools/capture/render_ce5.cpp src/dsp/ChorusProcessor.cpp -o build-ce5/render_ce5
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "ChorusProcessor.h"

int main(int argc, char** argv) {
    if (argc != 6) {
        std::fprintf(stderr, "usage: render_ce5 ELEVEL RATE_HZ DEPTH LOW HIGH < mono.f32 > two.f32\n");
        return 2;
    }
    namx::ChorusProcessor chorus;
    chorus.set_sample_rate(48000.0);
    chorus.set_enabled(true);
    chorus.set_level_db(namx::ce5::level_db_from_pedal(std::strtof(argv[1], nullptr)));
    chorus.set_rate_hz(std::strtof(argv[2], nullptr));
    chorus.set_depth_pct(100.0f * namx::ce5::depth_from_pedal(std::strtof(argv[3], nullptr)));
    chorus.set_low_cut_hz(namx::ce5::low_hz_from_pedal(std::strtof(argv[4], nullptr)));
    chorus.set_high_cut_hz(namx::ce5::high_hz_from_pedal(std::strtof(argv[5], nullptr)));
    std::vector<float> in;
    float buf[4096];
    size_t got;
    while ((got = std::fread(buf, sizeof(float), 4096, stdin)) > 0) {
        in.insert(in.end(), buf, buf + got);
    }
    const float ceiling = namx::ce5::db_to_gain(namx::ce5::REFERENCE_DBFS + namx::ce5::INPUT_CEILING_DB);
    const float dry_gain = namx::ce5::db_to_gain(namx::ce5::DRY_GAIN_DB);
    std::vector<float> dry(in), right(in);
    for (float& v : dry) {
        v = dry_gain * std::clamp(v, -ceiling, ceiling);
    }
    for (size_t done = 0; done < in.size(); done += 256) {
        const size_t n = std::min<size_t>(256, in.size() - done);
        chorus.process(in.data() + done, right.data() + done, n, false);
    }
    std::vector<float> both(2 * in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        both[2 * i] = in[i] - dry[i];
        both[2 * i + 1] = dry[i];
    }
    std::fwrite(both.data(), sizeof(float), both.size(), stdout);
    return 0;
}
