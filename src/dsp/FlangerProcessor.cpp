#include "FlangerProcessor.h"

#include <algorithm>
#include <cmath>

#include "Common.h"
#include "Constants.h"

namespace namx
{

static_assert(static_cast<float>(NAMConstants::FLANGER_RATE_MIN) == bf3::RATE_HZ.front() &&
                  static_cast<float>(NAMConstants::FLANGER_RATE_MAX) == bf3::RATE_HZ.back(),
              "the Rate knob spans the pedal's");

void FlangerProcessor::on_sample_rate_changed(double sr) {
    const size_t length = static_cast<size_t>(bf3::MAX_DELAY_MS * sr / 1000.0) + 4;
    line_l.resize(length);
    line_r.resize(length);
    second_l.resize(length);
    second_r.resize(length);
    refit_tone.design(bf3::REFIT_TONE, sr);
    refit_tone.gain *= bf3::db_to_gain(bf3::REFIT_TONE_DB);
    smoothing_k = bf3::smoothing_coefficient(sr);
    glide_k = static_cast<float>(1.0 - std::exp(-1.0 / (0.02 * sr)));
    reset();
}

void FlangerProcessor::sync_right_channel() {
    if (!line_l.empty()) {
        line_r.copy_from(line_l);
        second_r.copy_from(second_l);
    }
    channels[1] = channels[0];
}

void FlangerProcessor::reset() {
    line_l.clear();
    line_r.clear();
    second_l.clear();
    second_r.clear();
    lfo.reset();
    lfo_right.reset();
    right_stale = false;
    lfo_right_stale = true;
    channels[0].reset();
    channels[1].reset();
    sweep = block_settings().target;
}

void FlangerProcessor::type_edge(int current_type) {
    // A new mode starts from rest: its loop, filters and LFO, the sweep at the knobs.
    if (current_type != last_type) {
        last_type = current_type; // first: block_settings() reads it (the tone's delay offset)
        channels[0].reset();
        channels[1].reset();
        lfo.reset();
        lfo_right_stale = true;
        sweep = block_settings().target;
    }
}

FlangerProcessor::Block FlangerProcessor::block_settings() const {
    Block b;
    b.fs = static_cast<float>(sample_rate);
    const float manual = bf3::manual_position(manual_ms.load());
    const float depth = bf3::pedal_depth(std::clamp(depth_pct.load(), 0.0f, 100.0f) / 100.0f);
    const float res = bf3::res_position(std::max(feedback_pct.load(), 0.0f) / 100.0f);
    b.ultra = last_type == BF3_ULTRA;
    b.target.set(manual, depth, bf3::REFIT_DELAY_MS);
    b.phase_step = static_cast<double>(rate_hz.load()) / sample_rate;
    b.wet_level = std::clamp(wet_pct.load() / 100.0f, 0.0f, 1.0f);
    b.ceiling = bf3::db_to_gain(bf3::REFERENCE_DBFS + bf3::CEILING_DB);
    const float standard_res = bf3::knob_map(bf3::STANDARD_RES, res);
    if (b.ultra) {
        b.dry = bf3::db_to_gain(bf3::ULTRA_STAGE_DRY_DB);
        b.wet = bf3::db_to_gain(bf3::ULTRA_STAGE_WET_DB);
        b.res = bf3::ULTRA_RES_SCALE * standard_res;
        // The sweep's delay has the tone's own delay taken off once; half of it is short of
        // the d/2 stage's read by half of that.
        b.half_trim = (0.5f * bf3::REFIT_DELAY_MS + bf3::ULTRA_HALF_SHORT_MS) * b.fs / 1000.0f;
    } else {
        b.dry = bf3::db_to_gain(bf3::STANDARD_DRY_DB);
        b.wet = bf3::db_to_gain(bf3::STANDARD_WET_DB);
        b.res = standard_res;
    }
    return b;
}

float FlangerProcessor::next_delay_ms(const Block& b) {
    sweep.floor_ms += glide_k * (b.target.floor_ms - sweep.floor_ms);
    sweep.swing_ms += glide_k * (b.target.swing_ms - sweep.swing_ms);
    sweep.power = b.target.power;
    return sweep.floor_ms + sweep.swing_ms * lfo.next(b.phase_step, smoothing_k, sweep.power);
}

float FlangerProcessor::stage(DelayLine& line, BiquadChainState<bf3::TONE_SECTIONS>& state, float x,
                              float d, const Block& b) const {
    // This sample's wet feeds the loop: read before the push, so d − 1 behind the newest is d
    // behind x (d is over 5 samples, no instant loop).
    const float wet = state.process(line.interpolate(d - 1.0f), refit_tone);
    line.push(x + b.res * wet);
    return b.dry * x + b.wet * wet;
}

float FlangerProcessor::process_sample(DelayLine& line, DelayLine& second, Channel& ch, float x,
                                       float delay_ms, const Block& b) const {
    const float d = delay_ms * b.fs / 1000.0f;
    if (b.ultra) {
        // Two flangers in series: at d/2, then at d.
        x = stage(second, ch.second_tone, x, 0.5f * d - b.half_trim, b);
    }
    return std::clamp(stage(line, ch.out_tone, x, d, b), -b.ceiling, b.ceiling);
}

void FlangerProcessor::process_block_mono(float* buf, size_t nframes) {
    if (line_l.empty()) {
        return; // no sample rate yet (audit 2026-10-04, Fable item 17)
    }
    type_edge(type.load());
    right_stale = true;
    lfo_right_stale = true;
    const Block b = block_settings();
    for (size_t i = 0; i < nframes; ++i) {
        wet_level += glide_k * (b.wet_level - wet_level);
        const float pedal =
            process_sample(line_l, second_l, channels[0], buf[i], next_delay_ms(b), b);
        buf[i] = mix_with_dry(buf[i], pedal, wet_level);
    }
}

void FlangerProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    if (line_l.empty()) {
        return; // no sample rate yet (audit 2026-10-04, Fable item 17)
    }
    type_edge(type.load());
    right_stale = false; // EffectBase synced it on the way in
    lfo_right_stale = true;
    // Stereo input: the mono pedal on each channel, one LFO.
    const Block b = block_settings();
    for (size_t i = 0; i < nframes; ++i) {
        const float delay_ms = next_delay_ms(b);
        wet_level += glide_k * (b.wet_level - wet_level);
        l[i] = mix_with_dry(l[i], process_sample(line_l, second_l, channels[0], l[i], delay_ms, b),
                            wet_level);
        r[i] = mix_with_dry(r[i], process_sample(line_r, second_r, channels[1], r[i], delay_ms, b),
                            wet_level);
    }
}

void FlangerProcessor::process_block_stereo_out(float* l, float* r, size_t nframes) {
    if (line_l.empty()) {
        return; // no sample rate yet (audit 2026-10-04, Fable item 17)
    }
    type_edge(type.load());
    if (right_stale) {
        sync_right_channel();
        right_stale = false;
    }
    if (lfo_right_stale) {
        lfo_right.smoothed = lfo.smoothed; // settles on its own sweep within a tenth of a second
        lfo_right_stale = false;
    }
    // The pedal's two outputs: the same flanger on each side (a line and a loop each), the
    // right side's sweep an eighth of a cycle ahead.
    lfo_right.phase = lfo.phase + bf3::STEREO_LEAD;
    if (lfo_right.phase >= 1.0) {
        lfo_right.phase -= 1.0;
    }
    const Block b = block_settings();
    for (size_t i = 0; i < nframes; ++i) {
        const float x = l[i];
        const float left_ms = next_delay_ms(b);
        const float right_ms =
            sweep.floor_ms +
            sweep.swing_ms * lfo_right.next(b.phase_step, smoothing_k, sweep.power);
        wet_level += glide_k * (b.wet_level - wet_level);
        l[i] = mix_with_dry(x, process_sample(line_l, second_l, channels[0], x, left_ms, b),
                            wet_level);
        r[i] = mix_with_dry(x, process_sample(line_r, second_r, channels[1], x, right_ms, b),
                            wet_level);
    }
}

} // namespace namx
