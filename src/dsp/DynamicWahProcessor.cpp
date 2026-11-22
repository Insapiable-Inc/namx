#include "DynamicWahProcessor.h"

#include <algorithm>

#include "Common.h"

namespace namx
{

void DynamicWahProcessor::on_sample_rate_changed(double sr) {
    engine.set_sample_rate(sr);
    reset();
}

void DynamicWahProcessor::sync_right_channel() {
    channels[1] = channels[0];
}

void DynamicWahProcessor::reset() {
    channels[0].reset();
    channels[1].reset();
}

void DynamicWahProcessor::type_edge(int current_type) {
    // A new mode starts from rest.
    if (current_type != last_type) {
        reset();
        last_type = current_type;
    }
}

void DynamicWahProcessor::render(float* l, float* r, size_t nframes) {
    type_edge(type.load());
    const aw3::Mode mode = last_type == AW3_DOWN    ? aw3::Mode::DOWN
                           : last_type == AW3_SHARP ? aw3::Mode::SHARP
                                                    : aw3::Mode::UP;
    aw3::Settings settings;
    settings.set(mode, position_pct.load() / 100.0f, sensitivity_pct.load() / 100.0f,
                 decay_pct.load() / 100.0f);
    const float mix = std::clamp(mix_pct.load(), 0.0f, 100.0f) / 100.0f;
    engine.process(l, nframes, settings, mix, channels[0]);
    if (r != nullptr) {
        engine.process(r, nframes, settings, mix, channels[1]);
    }
}

void DynamicWahProcessor::process_block_mono(float* buf, size_t nframes) {
    render(buf, nullptr, nframes);
}

void DynamicWahProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    render(l, r, nframes);
}

} // namespace namx
