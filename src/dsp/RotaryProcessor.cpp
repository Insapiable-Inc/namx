#include "RotaryProcessor.h"

#include <array>

namespace namx
{

void RotaryProcessor::on_sample_rate_changed(double sr) {
    engine.prepare(sr); // allocates and resamples: not on the audio thread
}

void RotaryProcessor::reset() {
    engine.reset();
}

rt2::Settings RotaryProcessor::settings() const {
    rt2::Settings s;
    s.mode = type.load() - MODE_I;
    s.slow_hz = rt2::knob_law(rt2::SLOW_HZ, slow_pct.load() / 100.0f);
    s.fast_hz = rt2::knob_law(rt2::FAST_HZ, fast_pct.load() / 100.0f);
    s.fast = fast.load();
    s.slow_ramps = slow_ramps.load();
    s.drive = std::clamp(drive_pct.load() / 100.0f, 0.0f, 1.0f);
    const float balance = std::clamp(balance_pct.load() / 100.0f, 0.0f, 1.0f);
    s.horn_gain = rt2::knob_gain(rt2::BALANCE_HORN_DB, balance);
    s.drum_gain = rt2::knob_gain(rt2::BALANCE_DRUM_DB, balance);
    s.level = rt2::knob_gain(rt2::LEVEL_DB, std::clamp(level_pct.load() / 100.0f, 0.0f, 1.0f));
    return s;
}

void RotaryProcessor::process_block_mono(float* buf, size_t nframes) {
    // audio thread: the pedal's mono output is Output A
    engine.process(buf, buf, nullptr, nframes, settings());
}

void RotaryProcessor::process_block_stereo_out(float* l, float* r, size_t nframes) {
    // audio thread: one speaker, two microphones
    engine.process(l, l, r, nframes, settings());
}

void RotaryProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    // audio thread: a stereo input is summed, a cabinet has one speaker
    const rt2::Settings s = settings();
    std::array<float, rt2::Engine::CHUNK> sum{};
    for (size_t done = 0; done < nframes; done += rt2::Engine::CHUNK) {
        const size_t count = std::min(rt2::Engine::CHUNK, nframes - done);
        for (size_t i = 0; i < count; ++i) {
            sum[i] = 0.5f * (l[done + i] + r[done + i]);
        }
        engine.process(sum.data(), l + done, r + done, count, s);
    }
}

} // namespace namx
