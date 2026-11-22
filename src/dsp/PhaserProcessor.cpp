#include "PhaserProcessor.h"

#include <algorithm>
#include <cmath>

#include "Common.h"
#include "Constants.h"

namespace namx
{

void PhaserProcessor::on_sample_rate_changed(double sr) {
    glide_k = static_cast<float>(1.0 - std::exp(-1.0 / (0.02 * sr)));
    designed = false;
    reset();
}

void PhaserProcessor::sync_right_channel() {
    voices[1] = voices[0];
    right_stale = false;
}

void PhaserProcessor::reset() {
    lfo_phase = 0.0;
    for (ph3::Voice& v : voices) {
        v.reset();
    }
    fresh = true;
    loop_k = 0.0f;
    out_s = 1.0f;
    ladder_position = 0.0f;
    step_left = 0.0f;
    right_stale = false;
}

void PhaserProcessor::process_block_mono(float* buf, size_t nframes) {
    right_stale = true;
    render(buf, nullptr, nframes, false);
}

void PhaserProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    right_stale = false; // EffectBase synced it on the way in
    render(l, r, nframes, false);
}

void PhaserProcessor::process_block_stereo_out(float* l, float* r, size_t nframes) {
    if (right_stale) {
        sync_right_channel();
    }
    render(l, r, nframes, true);
}

void PhaserProcessor::type_edge(int current_type) {
    // A new mode starts from rest, its fixed filters designed for it.
    if (current_type != last_type || !designed) {
        const ph3::Voicing& v = ph3::voicing(current_type - FIRST_TYPE);
        for (ph3::Voice& voice : voices) {
            voice.reset();
            voice.design(v, static_cast<float>(sample_rate));
        }
        fresh = true;
        loop_k = 0.0f;
        out_s = 1.0f;
        step_left = 0.0f;
        designed = true;
        last_type = current_type;
    }
}

void PhaserProcessor::control(int mode, size_t voice_count, float offset, size_t samples,
                              float rate, float depth) {
    // `depth`: the position on the pedal's Depth (Rise and Fall's mix). Advance the clocks by `samples`, then point the voices' sections at where they are.
    const float fs = static_cast<float>(sample_rate);
    const int glide = fresh ? 0 : static_cast<int>(samples);
    fresh = false;
    lfo_phase += static_cast<double>(samples) * rate / sample_rate;
    lfo_phase -= std::floor(lfo_phase);
    if (mode == ph3::RISE || mode == ph3::FALL) {
        const float ladder = static_cast<float>(ph3::LADDER);
        const float step = static_cast<float>(samples) * rate / fs;
        ladder_position += mode == ph3::RISE ? step : -step;
        ladder_position -= ladder * std::floor(ladder_position / ladder);
        for (size_t c = 0; c < voice_count; ++c) {
            voices[c].ladder_depth(depth, fs, glide == 0);
            voices[c].ladder_to(ladder_position + static_cast<float>(c) * offset, fs, glide);
        }
        return;
    }
    if (mode == ph3::STEP) {
        const float hold = ph3::step_hold_ms(rate) * fs / 1000.0f;
        step_left = std::min(step_left - static_cast<float>(samples), hold);
        const bool sample = step_left <= 0.0f || glide == 0;
        if (step_left <= 0.0f) {
            step_left += hold;
        }
        for (size_t c = 0; c < voice_count; ++c) {
            ph3::Voice& voice = voices[c];
            if (sample) {
                voice.held =
                    ph3::triangle(static_cast<float>(lfo_phase) + static_cast<float>(c) * offset);
            }
            voice.sweep_to(sweep.corner_hz(voice.held), fs, glide);
        }
        return;
    }
    for (size_t c = 0; c < voice_count; ++c) {
        const float tri =
            ph3::triangle(static_cast<float>(lfo_phase) + static_cast<float>(c) * offset);
        voices[c].sweep_to(sweep.corner_hz(tri), fs, glide);
    }
}

void PhaserProcessor::render(float* l, float* r, size_t nframes, bool stereo_lfo) {
    if (sample_rate <= 0.0) {
        return; // no sample rate yet
    }
    type_edge(type.load());
    const int mode = last_type - FIRST_TYPE;
    const ph3::Voicing& v = ph3::voicing(mode);
    const float rate = std::max(rate_hz.load(), 0.0f);
    const float travel = std::clamp(depth_pct.load() / 100.0f, 0.0f, 1.0f);
    const float depth = ph3::depth_position(travel); // Rise / Fall: tabulated by position
    sweep.set(v, travel, rate);
    const float k_target = std::clamp(feedback_pct.load() / 100.0f, 0.0f, ph3::LOOP_GAIN_TOP);
    const float s_target = 1.0f - (1.0f - v.s_full) * k_target / v.k_full;
    const float wet_target = std::clamp(wet_pct.load() / 100.0f, 0.0f, 1.0f);
    const size_t voice_count = r != nullptr ? 2 : 1;
    const float offset = stereo_lfo ? 0.5f : 0.0f; // Rise / Fall: half a spacing
    for (size_t i = 0; i < nframes;) {
        const size_t m = std::min(CONTROL, nframes - i);
        control(mode, voice_count, offset, m, rate, depth);
        for (size_t j = i; j < i + m; ++j) {
            loop_k += glide_k * (k_target - loop_k);
            out_s += glide_k * (s_target - out_s);
            wet_level += glide_k * (wet_target - wet_level);
            l[j] = mix_with_dry(l[j], voices[0].process(l[j], v, loop_k, out_s), wet_level);
            if (r != nullptr) {
                r[j] = mix_with_dry(r[j], voices[1].process(r[j], v, loop_k, out_s), wet_level);
            }
        }
        i += m;
    }
}

} // namespace namx
