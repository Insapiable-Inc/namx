#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

// A model of a Boss AW-3 Dynamic Wah in its Up, Down and Sharp modes, fitted to captures of
// the real pedal (captures/boss/aw-3: the study, and tools/capture/analyze_aw3.py for Up).
//
// Detector (shared by the modes): the input, high-passed twice (one pole each, so the
// treble moves the filter more than the bass), is measured as RMS over 1 ms, limited, and
// followed twice side by side — a fast follower (a ninth of the control) and Decay's. The
// level opens a control that sets the
// filter's frequency (at low Sens an onset path joins it: ONSET_GAIN):
//   Up, Sharp: f = min(top, floor + (manual_offset + Sens·env)^power)  — Manual sets the
//              rest, the level pulls it up to a common 2.25 kHz top;
//   Down:      f = floor + max(0, manual_offset − Sens·env)^power      — Manual parks it
//              high (840 Hz–4 kHz), the level pulls it down to a common ~400 Hz floor.
// Up and Down share one frequency-indexed resonator (low·LP + gain·k·BP + high·HP, its Q
// falling from 14 at 194 Hz to 2.6 at 4 kHz, +14 dB at the peak). Sharp's filter is its
// own: two equal resonances (Q 5.4) with a cubic numerator — taller (+17 dB), steeper above
// the peak than below, and the same shape at every frequency. That structure is a fit of
// the measured response (1.1 dB rms), not the pedal's identified circuit or code.
// The output clips at a fixed asymmetric ceiling (+4.3/+3.9 dB over the guitar's peak).
// Not modelled: the pedal's 0.56 ms latency, its ~21 kHz wall, an input-side limit above
// the guitar's peak.
namespace namx::aw3
{

// A hard-played guitar's peak, as for the other pedal models: an assumption — the pedal
// sat before the amp. The fits' level unit (1.0 = the guitar's peak) maps here.
constexpr float REFERENCE_DBFS = -6.0f;

// The resonator, by its pole frequency (Up's noise and sweep fits; the last two points
// and 840 Hz are Down's parked sweeps; its top two gains set by rendering to its measured
// +14.4/+14.6 dB peaks). Interpolated linearly in Hz, as fitted.
constexpr int FILTER_POINTS = 10;
constexpr float FILTER_HZ[FILTER_POINTS] = {194, 229, 524, 631, 840, 1180, 1370, 2222, 2534, 4013};
constexpr float FILTER_Q[FILTER_POINTS] = {14.38f, 13.11f, 7.49f, 6.48f, 5.27f,
                                           3.6f,   3.2f,   2.9f,  2.76f, 2.62f};
constexpr float FILTER_GAIN[FILTER_POINTS] = {5.165f, 5.18f, 5.21f, 5.13f, 5.01f,
                                              5.05f,  5.05f, 5.08f, 5.1f,  5.15f};
constexpr float FILTER_LOW[FILTER_POINTS] = {0.03f, 0.035f, 0.069f, 0.08f,  0.11f,
                                             0.14f, 0.16f,  0.18f,  0.153f, 0.153f};
constexpr float FILTER_HIGH[FILTER_POINTS] = {0.0444f, 0.0499f, 0.103f, 0.126f, 0.19f,
                                              0.27f,   0.33f,   0.47f,  0.526f, 0.725f};

// Sharp: H(s) = (ALPHA + BETA·s + GAMMA·s²)(1 + DELTA·s) / (s² + s/Q + 1)², s normalized
// to the control frequency (the fitted cubic 0.27356 + 0.17002 s + 0.01724 s² + 0.14706 s³,
// factored for two cascaded state-variable filters).
constexpr float SHARP_Q = 5.355f;
constexpr float SHARP_ALPHA = 0.27356f;
constexpr float SHARP_BETA = -0.117085f;
constexpr float SHARP_GAMMA = 0.140122f;
constexpr float SHARP_DELTA = 1.049513f;

// Detector: refitted 2026-11-11 to the focused session (the pedal's filter read over time on
// guitar, tools/capture/aw3/focused.py) together with October's noise staircases. The first
// fit, on white noise alone, weighted the treble far more (corners 1168 Hz and 9 kHz): right
// on noise, but a guitar moved the filter up to an octave less than the pedal's. The noise
// jumps (the return after loud noise, five Decay positions and a sixth set blind) are in the
// fit too: one release fitted the guitar or the jumps, the two followers side by side both.
// A second jumps session (Sens at four positions) gave the control's cap, CONTROL_CAP.
constexpr float DETECTOR_HP_HZ[2] = {968.87f, 6800.26f};
constexpr float ATTACK_S = 0.00657f; // both followers
constexpr float FAST_RELEASE_S = 0.0005f;
constexpr float FAST_SHARE = 0.07868f; // of the level the control sees
constexpr float LEVEL_LIMIT = 0.02075f; // weighted 1 ms RMS, before Sens (-35 dB re peak)

// Decay 7:00, 9:00, 12:00, 3:00, 5:00 → the second follower's release.
constexpr int DECAY_POINTS = 5;
constexpr float DECAY_KNOB[DECAY_POINTS] = {0.0f, 0.2f, 0.5f, 0.8f, 1.0f};
constexpr float DECAY_S[DECAY_POINTS] = {0.25952f, 0.31132f, 0.31895f, 0.73711f, 1.36407f};

// Sens 7:00 … 5:00 in hour steps → the envelope's gain in control units.
constexpr int KNOB_POINTS = 11;
constexpr float SENS_GAIN[KNOB_POINTS] = {0.0f,    35.17f,   196.34f,  202.07f,  211.01f, 580.70f,
                                          698.24f, 1187.09f, 2177.19f, 3338.28f, 4039.24f};

// The onset path, at low Sens only (the check session, 2026-11-21: tools/capture/aw3/
// fit_onset.py). At Sens 9:00 every pick throws the pedal's filter to about the same place,
// that Sens position's ceiling, and it falls back within 0.2 s under a chord that holds its
// level; the followers above, right between hits and on noise, moved it 0.6-0.8 octave too
// little while a guitar played. The input through a one-pole high-pass (the bass counts
// here), its RMS over 1 ms smoothed, less that smoothed again: what has just risen. It adds
// to the control, the sum stopping at the ceiling.
constexpr float ONSET_HP_HZ = 174.5f;
constexpr float ONSET_ATTACK_S = 0.02533f;
constexpr float ONSET_RELEASE_S = 0.0859f;
// Sens 7:00 … 5:00 in hour steps → the onset path's gain (control units per unit of level).
// Measured at 9:00. None at 12:00 (measured: fitted there it comes out at 7 % of 9:00's and
// improves nothing), so none above. Not measured: 8:00 (SENS_GAIN's share of 9:00), 10:00 and
// 11:00 (fading out).
constexpr float ONSET_GAIN[KNOB_POINTS] = {0.0f, 17.59f, 98.2f, 65.47f, 32.73f, 0.0f,
                                           0.0f, 0.0f,   0.0f,  0.0f,   0.0f};

// Control: Up/Sharp.
constexpr float FLOOR_HZ = 191.338f;
constexpr float POWER = 3.05072f;
constexpr float TOP_HZ = 2249.33f;
// The control stops a quarter over what the top needs (in control units), whatever Sens is:
// after loud playing the filter leaves the top 90-170 ms later at every Sens (the jumps
// session, 2026-11-11). Without it the control climbed with Sens and held the top up to half
// a second longer. Down: the same margin past its floor (assumed, not measured).
constexpr float CONTROL_CAP = 1.25722f;
// Manual 7:00 … 5:00 in hour steps → Up's rest (pole) frequency.
constexpr float UP_MANUAL_HZ[KNOB_POINTS] = {193.74f, 195.24f, 209.40f, 217.98f, 228.92f, 228.91f,
                                             233.21f, 253.60f, 295.46f, 387.83f, 523.62f};
// Down: Manual 7:00, 12:00, 5:00 → its rest (interpolated in log frequency), and the floor
// the level pulls it down to.
constexpr float DOWN_MANUAL_HZ[3] = {840.0f, 2534.0f, 4013.0f};
constexpr float DOWN_FLOOR_HZ = 393.0f;

// The output's ceiling, re the guitar's peak (+4.3 / +3.9 dB).
constexpr float CEILING_POSITIVE = 1.64f;
constexpr float CEILING_NEGATIVE = 1.57f;

enum class Mode
{
    UP,
    DOWN,
    SHARP,
};

template <std::size_t N>
inline float table(const float (&xs)[N], const float (&ys)[N], float x) {
    if (x <= xs[0]) {
        return ys[0];
    }
    for (std::size_t i = 1; i < N; ++i) {
        if (x <= xs[i]) {
            const float t = (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
            return ys[i - 1] + t * (ys[i] - ys[i - 1]);
        }
    }
    return ys[N - 1];
}

// Knob fraction (0 = 7:00, 1 = 5:00) over an hour-step table.
inline float hourly(const float (&ys)[KNOB_POINTS], float fraction) {
    const float x = std::clamp(fraction, 0.0f, 1.0f) * (KNOB_POINTS - 1);
    const int i = std::min(static_cast<int>(x), KNOB_POINTS - 2);
    return ys[i] + (x - static_cast<float>(i)) * (ys[i + 1] - ys[i]);
}

// Where the filter rests for a Manual position (0 = 7:00, 1 = 5:00), Hz: Up and Sharp on the
// hourly table, Down between its three measured rests in log frequency.
inline float rest_hz(Mode mode, float manual) {
    const float m = std::clamp(manual, 0.0f, 1.0f);
    if (mode != Mode::DOWN) {
        return hourly(UP_MANUAL_HZ, m);
    }
    const float lo = m < 0.5f ? DOWN_MANUAL_HZ[0] : DOWN_MANUAL_HZ[1];
    const float hi = m < 0.5f ? DOWN_MANUAL_HZ[1] : DOWN_MANUAL_HZ[2];
    const float t = m < 0.5f ? 2.0f * m : 2.0f * m - 1.0f;
    return lo * std::pow(hi / lo, t);
}

// The return's time constant for a Decay position (0 = 7:00, 1 = 5:00), seconds.
inline float decay_seconds(float decay_knob) {
    return table(DECAY_KNOB, DECAY_S, std::clamp(decay_knob, 0.0f, 1.0f));
}

// What one block needs from the knobs (computed once per block).
struct Settings
{
    Mode mode = Mode::UP;
    float manual_offset = 0.0f; // control units
    float sens_gain = 0.0f;
    float level_cap = LEVEL_LIMIT; // the most level the followers see
    float onset_gain = 0.0f; // 0: no onset path at this Sens
    float onset_attack = 0.0f; // steps per 1 ms tick
    float onset_release = 0.0f;
    float attack = 0.0f; // follower steps per 1 ms tick
    float fast_release = 0.0f;
    float decay = 0.0f;

    void set(Mode new_mode, float manual, float sens, float decay_knob) {
        mode = new_mode;
        const float rest = rest_hz(mode, manual);
        if (mode == Mode::DOWN) {
            manual_offset = std::pow(rest - DOWN_FLOOR_HZ, 1.0f / POWER);
        } else {
            manual_offset = std::pow(std::max(rest - FLOOR_HZ, 0.0f), 1.0f / POWER);
        }
        sens_gain = hourly(SENS_GAIN, sens);
        const float top = std::pow(TOP_HZ - FLOOR_HZ, 1.0f / POWER);
        const float travel = mode == Mode::DOWN ? manual_offset : top - manual_offset;
        level_cap = LEVEL_LIMIT;
        if (sens_gain > 0.0f) {
            level_cap = std::min(LEVEL_LIMIT,
                                 std::max(travel + (CONTROL_CAP - 1.0f) * top, 0.0f) / sens_gain);
        }
        onset_gain = hourly(ONSET_GAIN, sens);
        onset_attack = 1.0f - std::exp(-0.001f / ONSET_ATTACK_S);
        onset_release = 1.0f - std::exp(-0.001f / ONSET_RELEASE_S);
        attack = 1.0f - std::exp(-0.001f / ATTACK_S);
        fast_release = 1.0f - std::exp(-0.001f / FAST_RELEASE_S);
        decay = 1.0f - std::exp(-0.001f / decay_seconds(decay_knob));
    }

    // envelope: the level the followers give; onset: what the onset path adds, control units.
    float frequency(float envelope, float onset = 0.0f) const {
        float control = sens_gain * envelope;
        if (onset > 0.0f) {
            control = std::min(control + onset, sens_gain * level_cap);
        }
        if (mode == Mode::DOWN) {
            return DOWN_FLOOR_HZ + std::pow(std::max(0.0f, manual_offset - control), POWER);
        }
        return std::min(TOP_HZ, FLOOR_HZ + std::pow(manual_offset + control, POWER));
    }
};

// One channel's state. The control updates every 1 ms (as fitted); the filter's
// coefficients ramp linearly to each new value over the next millisecond.
struct Channel
{
    float hp_state[2]{};
    float sum_squares = 0.0f;
    int count = 0;
    float fast = 0.0f;
    float envelope = 0.0f;
    // The onset path: its high-pass, its 1 ms sum, the level smoothed once and twice.
    float onset_hp_state = 0.0f;
    float onset_sum_squares = 0.0f;
    float onset_level = 0.0f;
    float onset_slow = 0.0f;
    bool primed = false;
    // Filter coefficients (current, per-sample step): g, k, BP weight, LP weight, HP weight.
    float coef[5]{};
    float step[5]{};
    float svf[4]{};

    // The level the control sees: a small share that follows the playing at once, the rest
    // through Decay.
    float control() const {
        return FAST_SHARE * fast + (1.0f - FAST_SHARE) * envelope;
    }

    void reset() {
        *this = Channel{};
    }
};

class Engine
{
public:
    void set_sample_rate(double rate) {
        fs = static_cast<float>(rate);
        samples_per_tick = std::max(1, static_cast<int>(std::lround(rate / 1000.0)));
        for (int i = 0; i < 2; ++i) {
            const float g = std::tan(std::numbers::pi_v<float> * DETECTOR_HP_HZ[i] / fs);
            hp_coef[i] = g / (1.0f + g);
        }
        const float g = std::tan(std::numbers::pi_v<float> * ONSET_HP_HZ / fs);
        onset_hp_coef = g / (1.0f + g);
    }

    void process(float* buf, size_t nframes, const Settings& s, float mix, Channel& c) const {
        const float ref = std::pow(10.0f, REFERENCE_DBFS / 20.0f);
        const float inv_ref = 1.0f / ref;
        if (!c.primed) {
            targets(s, s.frequency(c.control()), c.coef);
            c.primed = true;
        }
        for (size_t i = 0; i < nframes; ++i) {
            const float dry = buf[i];
            const float x = dry * inv_ref;

            // Detector: two one-pole high-passes (TPT), RMS over 1 ms.
            float h = x;
            for (int j = 0; j < 2; ++j) {
                const float v = (h - c.hp_state[j]) * hp_coef[j];
                const float lp = v + c.hp_state[j];
                c.hp_state[j] = lp + v;
                h -= lp;
            }
            c.sum_squares += h * h;
            if (s.onset_gain > 0.0f) {
                const float v = (x - c.onset_hp_state) * onset_hp_coef;
                const float lp = v + c.onset_hp_state;
                c.onset_hp_state = lp + v;
                c.onset_sum_squares += (x - lp) * (x - lp);
            }
            if (++c.count == samples_per_tick) {
                tick(s, c);
            }

            const float wet = s.mode == Mode::SHARP ? sharp(x, c) : resonator(x, c);
            for (int j = 0; j < 5; ++j) {
                c.coef[j] += c.step[j];
            }
            const float clipped = std::clamp(wet, -CEILING_NEGATIVE, CEILING_POSITIVE) * ref;
            buf[i] = mix * clipped + (1.0f - mix) * dry;
        }
    }

private:
    float fs = 48000.0f;
    int samples_per_tick = 48;
    float hp_coef[2]{};
    float onset_hp_coef = 0.0f;

    void tick(const Settings& s, Channel& c) const {
        const float level =
            std::min(std::sqrt(c.sum_squares / static_cast<float>(samples_per_tick)), s.level_cap);
        c.sum_squares = 0.0f;
        c.count = 0;
        c.fast += (level - c.fast) * (level > c.fast ? s.attack : s.fast_release);
        c.envelope += (level - c.envelope) * (level > c.envelope ? s.attack : s.decay);
        float onset = 0.0f;
        if (s.onset_gain > 0.0f) {
            const float quick =
                std::sqrt(c.onset_sum_squares / static_cast<float>(samples_per_tick));
            c.onset_level += (quick - c.onset_level) * s.onset_attack;
            c.onset_slow += (c.onset_level - c.onset_slow) * s.onset_release;
            onset = s.onset_gain * std::max(c.onset_level - c.onset_slow, 0.0f);
        }
        c.onset_sum_squares = 0.0f;
        float target[5];
        targets(s, s.frequency(c.control(), onset), target);
        const float per_sample = 1.0f / static_cast<float>(samples_per_tick);
        for (int j = 0; j < 5; ++j) {
            c.step[j] = (target[j] - c.coef[j]) * per_sample;
        }
    }

    void targets(const Settings& s, float hz, float* out) const {
        hz = std::min(hz, 0.45f * fs);
        out[0] = std::tan(std::numbers::pi_v<float> * hz / fs);
        if (s.mode == Mode::SHARP) {
            out[1] = 1.0f / SHARP_Q;
            out[2] = out[3] = out[4] = 0.0f;
            return;
        }
        const float k = 1.0f / table(FILTER_HZ, FILTER_Q, hz);
        out[1] = k;
        out[2] = table(FILTER_HZ, FILTER_GAIN, hz) * k;
        out[3] = table(FILTER_HZ, FILTER_LOW, hz);
        out[4] = table(FILTER_HZ, FILTER_HIGH, hz);
    }

    // Trapezoidal state-variable filter: v1 = s/D (band), v2 = 1/D (low), D = s² + k·s + 1.
    static void svf(float x, float g, float k, float& s1, float& s2, float& v1, float& v2) {
        v1 = (s1 + g * (x - s2)) / (1.0f + g * (g + k));
        v2 = s2 + g * v1;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
    }

    static float resonator(float x, Channel& c) {
        const float g = c.coef[0], k = c.coef[1];
        float v1, v2;
        svf(x, g, k, c.svf[0], c.svf[1], v1, v2);
        const float hp = x - k * v1 - v2;
        return c.coef[3] * v2 + c.coef[2] * v1 + c.coef[4] * hp;
    }

    static float sharp(float x, Channel& c) {
        const float g = c.coef[0], k = c.coef[1];
        float v1, v2;
        svf(x, g, k, c.svf[0], c.svf[1], v1, v2);
        const float hp = x - k * v1 - v2;
        const float w = SHARP_ALPHA * v2 + SHARP_BETA * v1 + SHARP_GAMMA * hp;
        svf(w, g, k, c.svf[2], c.svf[3], v1, v2);
        return v2 + SHARP_DELTA * v1;
    }
};

} // namespace namx::aw3
