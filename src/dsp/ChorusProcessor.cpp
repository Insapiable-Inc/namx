#include "ChorusProcessor.h"

#include <algorithm>

#include "Common.h"
#include "Constants.h"

namespace namx
{

static_assert(static_cast<float>(NAMConstants::CHORUS_RATE_MIN) == ce5::RATE_HZ.front() &&
                  static_cast<float>(NAMConstants::CHORUS_RATE_MAX) == ce5::RATE_HZ.back(),
              "the Rate knob spans the pedal's");

void ChorusProcessor::on_sample_rate_changed(double sr) {
    const size_t length = static_cast<size_t>(MAX_DELAY_MS * sr / 1000.0f) + 4;
    line_l.resize(length);
    line_r.resize(length);
    tone.prepare(sr);
    reset();
}

void ChorusProcessor::sync_right_channel() {
    if (!line_l.empty()) {
        line_r.copy_from(line_l);
    }
    tone_state[1] = tone_state[0];
}

void ChorusProcessor::reset() {
    line_l.clear();
    line_r.clear();
    lfo_phase = 0.0;
    right_stale = false;
    tone_state[0].reset();
    tone_state[1].reset();
}

ChorusProcessor::Block ChorusProcessor::block_settings() {
    Block b;
    b.fs = static_cast<float>(sample_rate);
    const float rate = rate_hz.load();
    b.sweep.set(ce5::pedal_depth(depth_pct.load() / 100.0f), rate);
    b.ph_inc = static_cast<double>(rate) / sample_rate;
    const float level = level_db.load();
    b.level = level <= static_cast<float>(NAMConstants::CHORUS_LEVEL_MIN)
                  ? 0.0f
                  : ce5::db_to_gain(level + ce5::DRY_GAIN_DB);
    b.input_ceiling = ce5::db_to_gain(ce5::REFERENCE_DBFS + ce5::INPUT_CEILING_DB);
    b.wet_ceiling = ce5::db_to_gain(ce5::REFERENCE_DBFS + ce5::WET_CEILING_DB);
    b.dry_gain = ce5::db_to_gain(ce5::DRY_GAIN_DB);
    tone.set_knobs(ce5::low_position(low_cut_hz.load()), ce5::high_position(high_cut_hz.load()),
                   sample_rate);
    return b;
}

float ChorusProcessor::read_wet(const DelayLine& line, ce5::ToneState& state, float phase,
                                const Block& b) {
    const float delayed = line.interpolate(b.sweep.delay_ms(phase) * b.fs / 1000.0f);
    const float wet =
        std::clamp(state.before_clip(b.level * delayed, tone), -b.wet_ceiling, b.wet_ceiling);
    return state.after_clip(wet, tone);
}

ChorusProcessor::Output ChorusProcessor::process_sample(DelayLine& line, ce5::ToneState& state,
                                                        float x, float phase, const Block& b) {
    const float in = std::clamp(x, -b.input_ceiling, b.input_ceiling);
    line.push(in);
    return {b.dry_gain * in, read_wet(line, state, phase, b)};
}

void ChorusProcessor::advance_lfo(const Block& b) {
    lfo_phase += b.ph_inc;
    if (lfo_phase >= 1.0) {
        lfo_phase -= 1.0;
    }
}

void ChorusProcessor::process_block_mono(float* buf, size_t nframes) {
    if (line_l.empty()) {
        return; // no sample rate yet (audit 2026-10-04, Fable item 17)
    }
    right_stale = true;
    const Block b = block_settings();
    for (size_t i = 0; i < nframes; ++i) {
        const Output o =
            process_sample(line_l, tone_state[0], buf[i], static_cast<float>(lfo_phase), b);
        buf[i] = o.dry + o.wet;
        advance_lfo(b);
    }
}

void ChorusProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    if (line_l.empty()) {
        return; // no sample rate yet (audit 2026-10-04, Fable item 17)
    }
    right_stale = false; // EffectBase synced it on the way in
    // Stereo input: the mono pedal on each channel, one LFO.
    const Block b = block_settings();
    for (size_t i = 0; i < nframes; ++i) {
        const float phase = static_cast<float>(lfo_phase);
        const Output o_l = process_sample(line_l, tone_state[0], l[i], phase, b);
        const Output o_r = process_sample(line_r, tone_state[1], r[i], phase, b);
        l[i] = o_l.dry + o_l.wet;
        r[i] = o_r.dry + o_r.wet;
        advance_lfo(b);
    }
}

void ChorusProcessor::process_block_stereo_out(float* l, float* r, size_t nframes) {
    if (line_l.empty()) {
        return; // no sample rate yet (audit 2026-10-04, Fable item 17)
    }
    if (right_stale) {
        tone_state[1] = tone_state[0];
        right_stale = false;
    }
    // The mono pedal on each side, from one line: the right side's sweep half a cycle behind.
    const Block b = block_settings();
    for (size_t i = 0; i < nframes; ++i) {
        const float phase = static_cast<float>(lfo_phase);
        const Output o = process_sample(line_l, tone_state[0], l[i], phase, b);
        const float right_phase = phase >= 0.5f ? phase - 0.5f : phase + 0.5f;
        l[i] = o.dry + o.wet;
        r[i] = o.dry + read_wet(line_l, tone_state[1], right_phase, b);
        advance_lfo(b);
    }
}

} // namespace namx
