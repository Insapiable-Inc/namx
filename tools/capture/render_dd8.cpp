// NAM-X's Delay (the DD-8 models, DelayProcessor) rendered offline: a capture's played signal
// through the processor exactly as the app runs it, mono (OUTPUT A alone), for checking it
// against the pedal (tools/capture/dd8/focused.py).
//
//   render_dd8 TYPE TIME_MS LEVEL_POSITION FEEDBACK_POSITION < mono.f32 > mono.f32
//   render_dd8 TYPE TIME_MS LEVEL_POSITION FEEDBACK_POSITION R_TIME_MS < mono.f32 > stereo.f32
//
// With R_TIME_MS: Stereo on (both outputs connected), the output interleaved left, right; the
// pedal's ping-pong is R_TIME_MS = twice TIME_MS.
//
// TYPE 1–5: Standard, Analog, Tape, Warm, Mod. TIME_MS: the delay as measured on the take (the
// pedal's Time knob isn't mapped: NAM-X's is in ms). LEVEL_POSITION and FEEDBACK_POSITION: the
// pedal's knobs, 0–1 of their rotation (7:00–5:00), turned into NAM-X's Level (dB) and Feedback
// (% of gain per round, which stops at 100: the pedal's 5:00, a little over, is a hold here). Input and output: raw float32 mono at 48 kHz in NAM-X's scale, processed
// in 256-frame blocks. Build from the repository root:
//
//   g++ -std=c++23 -O2 -Isrc/dsp tools/capture/render_dd8.cpp src/dsp/DelayProcessor.cpp -o build-dd8/render_dd8
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "Constants.h"
#include "DelayProcessor.h"

int main(int argc, char** argv) {
    if (argc != 5 && argc != 6) {
        std::fprintf(stderr, "usage: render_dd8 TYPE TIME_MS LEVEL_POSITION FEEDBACK_POSITION "
                             "< mono.f32 > mono.f32\n");
        return 2;
    }
    namx::DelayProcessor delay;
    delay.set_sample_rate(48000.0);
    delay.set_enabled(true);
    const bool stereo = argc == 6;
    delay.set_stereo_mode(stereo);
    if (stereo) {
        delay.set_right_time(std::strtof(argv[5], nullptr));
    }
    delay.set_type(std::atoi(argv[1]));
    delay.set_time(std::strtof(argv[2], nullptr));
    // The pedal's positions as NAM-X's knobs: the level in dB, the gain per round in %.
    const namx::dd8::Voicing& voicing =
        *namx::dd8::VOICINGS[static_cast<size_t>(std::clamp(std::atoi(argv[1]), 1, 5) - 1)];
    const float level_position = std::strtof(argv[3], nullptr);
    delay.set_level_db(level_position <= 0.0f
                           ? static_cast<float>(namx::NAMConstants::DELAY_LEVEL_MIN)
                           : namx::dd8::level_db_from_pedal(voicing, level_position));
    delay.set_feedback_pct(namx::dd8::feedback_pct_from_pedal(voicing, std::strtof(argv[4], nullptr)));
    std::vector<float> in;
    float buf[4096];
    size_t got;
    while ((got = std::fread(buf, sizeof(float), 4096, stdin)) > 0) {
        in.insert(in.end(), buf, buf + got);
    }
    std::vector<float> right(in);
    for (size_t done = 0; done < in.size(); done += 256) {
        const size_t n = std::min<size_t>(256, in.size() - done);
        delay.process(in.data() + done, right.data() + done, n, false);
    }
    if (!stereo) {
        std::fwrite(in.data(), sizeof(float), in.size(), stdout);
        return 0;
    }
    std::vector<float> both(2 * in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        both[2 * i] = in[i];
        both[2 * i + 1] = right[i];
    }
    std::fwrite(both.data(), sizeof(float), both.size(), stdout);
    return 0;
}
