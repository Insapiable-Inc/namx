#include "Metronome.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>

#include "Common.h"

namespace namx
{

Metronome::Metronome()
    : accents(default_accents(4, 4)) {
    build_sounds(sample_rate);
}

void Metronome::set_sample_rate(double new_sr) {
    if (new_sr <= 0.0 || new_sr == sample_rate) {
        return;
    }
    // Called with processing suspended (on_buffer_size_changed); drop voices
    // that point into the buffers about to be replaced.
    for (auto& v : voices) {
        v = {};
    }
    sample_rate = new_sr;
    build_sounds(new_sr);
}

void Metronome::set_meter(int new_beats, int new_unit) {
    beats.store(std::clamp(new_beats, 1, MAX_BEATS));
    unit.store(new_unit == 2 || new_unit == 8 || new_unit == 16 ? new_unit : 4);
}

std::uint32_t Metronome::default_accents(int beats, int unit) {
    beats = std::clamp(beats, 1, MAX_BEATS);
    const bool compound = unit >= 8 && beats > 3 && beats % 3 == 0;
    std::uint32_t p = 0;
    for (int b = 0; b < beats; ++b) {
        Accent a = b == 0     ? Accent::STRONG
                   : compound ? (b % 3 == 0 ? Accent::NORMAL : Accent::SOFT)
                              : Accent::NORMAL;
        p = with_accent(p, b, a);
    }
    return p;
}

// Peak-normalize to 0.9 so every set sits at the same level.
static void normalize(vector<float>& s) {
    float peak = 0.0f;
    for (float v : s) {
        peak = std::max(peak, std::abs(v));
    }
    if (peak > 0.0f) {
        for (float& v : s) {
            v *= 0.9f / peak;
        }
    }
}

static vector<float> sine_blip(double sr, double freq, double len_s, double tau, double attack_s) {
    vector<float> s(static_cast<size_t>(len_s * sr));
    for (size_t i = 0; i < s.size(); ++i) {
        const double t = static_cast<double>(i) / sr;
        const double env = std::min(1.0, t / attack_s) * std::exp(-t / tau);
        s[i] = static_cast<float>(std::sin((2.0 * std::numbers::pi) * freq * t) * env);
    }
    normalize(s);
    return s;
}

// Noise burst into a resonant band-pass (RBJ): a woodblock-ish knock.
static vector<float> wood_knock(double sr, double freq, double len_s) {
    vector<float> s(static_cast<size_t>(len_s * sr));
    const double w0 = (2.0 * std::numbers::pi) * freq / sr, q = 9.0,
                 alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    const double b0 = alpha / a0, b2 = -alpha / a0;
    const double a1 = -2.0 * std::cos(w0) / a0, a2 = (1.0 - alpha) / a0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    std::uint32_t seed = 0x1234567u;
    for (size_t i = 0; i < s.size(); ++i) {
        seed = seed * 1664525u + 1013904223u;
        const double noise = (static_cast<double>(seed >> 8) / 8388608.0 - 1.0) *
                             std::exp(-(static_cast<double>(i) / sr) / 0.0015);
        const double y = b0 * noise + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = noise;
        y2 = y1;
        y1 = y;
        s[i] = static_cast<float>(y);
    }
    normalize(s);
    return s;
}

void Metronome::build_sounds(double sr) {
    samples[std::to_underlying(Sound::CLICK)] = {sine_blip(sr, 2000.0, 0.030, 0.006, 0.0005),
                                                 sine_blip(sr, 1500.0, 0.030, 0.006, 0.0005),
                                                 sine_blip(sr, 1000.0, 0.020, 0.004, 0.0005)};
    samples[std::to_underlying(Sound::WOOD)] = {
        wood_knock(sr, 1700.0, 0.060), wood_knock(sr, 1150.0, 0.060), wood_knock(sr, 850.0, 0.045)};
    samples[std::to_underlying(Sound::BEEP)] = {sine_blip(sr, 1760.0, 0.070, 0.050, 0.002),
                                                sine_blip(sr, 880.0, 0.070, 0.050, 0.002),
                                                sine_blip(sr, 660.0, 0.045, 0.030, 0.002)};
}

void Metronome::trigger(const vector<float>& sample, float gain) {
    // Free voice, else steal the older one.
    Voice* v = &voices[0];
    if (voices[0].data && (!voices[1].data || voices[1].pos > voices[0].pos)) {
        v = &voices[1];
    }
    v->data = sample.data();
    v->len = sample.size();
    v->pos = 0;
    v->gain = gain;
}

void Metronome::render(float* l, float* r, size_t from, size_t to, float g_l, float g_r) {
    for (auto& v : voices) {
        if (!v.data) {
            continue;
        }
        for (size_t i = from; i < to; ++i) {
            const float s = v.data[v.pos] * v.gain;
            l[i] += s * g_l;
            r[i] += s * g_r;
            if (++v.pos >= v.len) {
                v.data = nullptr;
                break;
            }
        }
    }
}

void Metronome::process(float* l, float* r, size_t n) {
    const bool run = running.load(std::memory_order_relaxed);
    if (run && !was_running) {
        until_next = 0.0; // first click on the first sample
        tick = 0;
        beat = 0;
        count_in_left = count_in_bars.load(std::memory_order_relaxed);
    } else if (!run && was_running) {
        beat_now.store(-1, std::memory_order_relaxed);
        count_in_now.store(false, std::memory_order_relaxed);
    }
    was_running = run;
    if (!run && !voices[0].data && !voices[1].data) {
        return;
    }

    const float vol = std::pow(10.0f, volume_db.load(std::memory_order_relaxed) / 20.0f);
    const float current_pan = std::clamp(pan.load(std::memory_order_relaxed), -1.0f, 1.0f);
    const float g_l =
        vol * std::cos((current_pan + 1.0f) * 0.25f * std::numbers::pi_v<float>); // constant power
    const float g_r = vol * std::sin((current_pan + 1.0f) * 0.25f * std::numbers::pi_v<float>);

    if (!run) {
        render(l, r, 0, n, g_l, g_r); // let the last click ring out
        return;
    }

    const int current_beats = beats.load(std::memory_order_relaxed);
    const int current_subdiv = subdiv.load(std::memory_order_relaxed);
    const double current_bpm = std::clamp(bpm.load(std::memory_order_relaxed), 20.0, 400.0);
    const double samples_per_tick = sample_rate * 60.0 / current_bpm *
                                    (4.0 / unit.load(std::memory_order_relaxed)) / current_subdiv;
    const std::uint32_t acc = accents.load(std::memory_order_relaxed);
    const array<vector<float>, 3>& set = samples[sound.load(std::memory_order_relaxed)];
    const bool current_silent_after = silent_after.load(std::memory_order_relaxed) &&
                                      count_in_bars.load(std::memory_order_relaxed) > 0;

    // Tick positions are fractional samples; `frames` is the block length on that scale.
    const double frames = static_cast<double>(n);
    size_t i = 0;
    double until = until_next;
    while (until < frames) {
        const size_t at = std::max(i, static_cast<size_t>(until));
        render(l, r, i, at, g_l, g_r);
        i = at;

        if (beat >= current_beats) {
            beat = 0; // meter shortened while running
        }
        const bool counting = count_in_left > 0;
        const bool audible = counting || !current_silent_after;
        const Accent a =
            counting ? (beat == 0 ? Accent::STRONG : Accent::NORMAL) : accent_of(acc, beat);
        if (tick == 0) {
            beat_now.store(beat, std::memory_order_relaxed);
            count_in_now.store(counting, std::memory_order_relaxed);
            if (audible && a != Accent::MUTE) {
                trigger(set[a == Accent::STRONG ? 0 : 1], a == Accent::SOFT ? 0.45f : 1.0f);
            }
        } else if (audible && a != Accent::MUTE) {
            trigger(set[2], 0.55f);
        }

        if (++tick >= current_subdiv) {
            tick = 0;
            if (++beat >= current_beats) {
                beat = 0;
                if (count_in_left > 0) {
                    --count_in_left;
                }
            }
        }
        until += samples_per_tick;
    }
    render(l, r, i, n, g_l, g_r);
    until_next = until - frames;
}

} // namespace namx
