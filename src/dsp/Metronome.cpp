#include "Metronome.h"

#include <algorithm>
#include <cmath>

static constexpr double K_TWO_PI = 6.283185307179586;

Metronome::Metronome()
    : m_accents(default_accents(4, 4)) {
    build_sounds(m_sr);
}

void Metronome::set_sample_rate(double sr) {
    if (sr <= 0.0 || sr == m_sr) {
        return;
    }
    // Called with processing suspended (on_buffer_size_changed); drop voices
    // that point into the buffers about to be replaced.
    for (auto& v : m_voices) {
        v = {};
    }
    m_sr = sr;
    build_sounds(sr);
}

void Metronome::set_meter(int beats, int unit) {
    m_beats.store(std::clamp(beats, 1, K_MAX_BEATS));
    m_unit.store(unit == 2 || unit == 8 || unit == 16 ? unit : 4);
}

uint32_t Metronome::default_accents(int beats, int unit) {
    beats = std::clamp(beats, 1, K_MAX_BEATS);
    const bool compound = unit >= 8 && beats > 3 && beats % 3 == 0;
    uint32_t p = 0;
    for (int b = 0; b < beats; ++b) {
        Accent a = b == 0 ? STRONG : compound ? (b % 3 == 0 ? NORMAL : SOFT) : NORMAL;
        p = with_accent(p, b, a);
    }
    return p;
}

// ---------------------------------------------------------------------------
// Sound synthesis (non-RT)
// ---------------------------------------------------------------------------

namespace
{

// Peak-normalize to 0.9 so every set sits at the same level.
void normalize(std::vector<float>& s) {
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

std::vector<float> sine_blip(double sr, double freq, double len_s, double tau, double attack_s) {
    std::vector<float> s(static_cast<size_t>(len_s * sr));
    for (size_t i = 0; i < s.size(); ++i) {
        const double t = static_cast<double>(i) / sr;
        const double env = std::min(1.0, t / attack_s) * std::exp(-t / tau);
        s[i] = static_cast<float>(std::sin(K_TWO_PI * freq * t) * env);
    }
    normalize(s);
    return s;
}

// Noise burst into a resonant band-pass (RBJ): a woodblock-ish knock.
std::vector<float> wood_knock(double sr, double freq, double len_s) {
    std::vector<float> s(static_cast<size_t>(len_s * sr));
    const double w0 = K_TWO_PI * freq / sr, q = 9.0, alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    const double b0 = alpha / a0, b2 = -alpha / a0;
    const double a1 = -2.0 * std::cos(w0) / a0, a2 = (1.0 - alpha) / a0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    uint32_t seed = 0x1234567u;
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

// Two detuned square waves through a soft band-limit — the TR-808 recipe.
std::vector<float> cowbell(double sr, double f1, double f2, double len_s) {
    std::vector<float> s(static_cast<size_t>(len_s * sr));
    double lp = 0.0;
    const double k = 1.0 - std::exp(-K_TWO_PI * 3500.0 / sr);
    for (size_t i = 0; i < s.size(); ++i) {
        const double t = static_cast<double>(i) / sr;
        const double sq = (std::sin(K_TWO_PI * f1 * t) >= 0 ? 1.0 : -1.0) +
                          (std::sin(K_TWO_PI * f2 * t) >= 0 ? 1.0 : -1.0);
        const double env =
            std::min(1.0, t / 0.001) * (0.7 * std::exp(-t / 0.012) + 0.3 * std::exp(-t / 0.09));
        lp += k * (sq * env - lp);
        s[i] = static_cast<float>(lp);
    }
    normalize(s);
    return s;
}

} // namespace

void Metronome::build_sounds(double sr) {
    m_samples[CLICK] = {sine_blip(sr, 2000.0, 0.030, 0.006, 0.0005),
                        sine_blip(sr, 1500.0, 0.030, 0.006, 0.0005),
                        sine_blip(sr, 1000.0, 0.020, 0.004, 0.0005)};
    m_samples[WOOD] = {wood_knock(sr, 1700.0, 0.060), wood_knock(sr, 1150.0, 0.060),
                       wood_knock(sr, 850.0, 0.045)};
    m_samples[BEEP] = {sine_blip(sr, 1760.0, 0.070, 0.050, 0.002),
                       sine_blip(sr, 880.0, 0.070, 0.050, 0.002),
                       sine_blip(sr, 660.0, 0.045, 0.030, 0.002)};
    m_samples[COWBELL] = {cowbell(sr, 587.0, 845.0, 0.200), cowbell(sr, 540.0, 800.0, 0.200),
                          cowbell(sr, 540.0, 800.0, 0.090)};
}

// ---------------------------------------------------------------------------
// Audio thread
// ---------------------------------------------------------------------------

void Metronome::trigger(const std::vector<float>& sample, float gain) {
    // Free voice, else steal the older one.
    Voice* v = &m_voices[0];
    if (m_voices[0].data && (!m_voices[1].data || m_voices[1].pos > m_voices[0].pos)) {
        v = &m_voices[1];
    }
    v->data = sample.data();
    v->len = sample.size();
    v->pos = 0;
    v->gain = gain;
}

void Metronome::render(float* l, float* r, size_t from, size_t to, float g_l, float g_r) {
    for (auto& v : m_voices) {
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
    const bool run = m_running.load(std::memory_order_relaxed);
    if (run && !m_was_running) {
        m_until_next = 0.0; // first click on the first sample
        m_tick = 0;
        m_beat = 0;
        m_count_in_left = m_count_in_bars.load(std::memory_order_relaxed);
    } else if (!run && m_was_running) {
        m_beat_now.store(-1, std::memory_order_relaxed);
        m_count_in_now.store(false, std::memory_order_relaxed);
    }
    m_was_running = run;
    if (!run && !m_voices[0].data && !m_voices[1].data) {
        return;
    }

    const float vol = std::pow(10.0f, m_volume_db.load(std::memory_order_relaxed) / 20.0f);
    const float pan = std::clamp(m_pan.load(std::memory_order_relaxed), -1.0f, 1.0f);
    const float g_l = vol * std::cos((pan + 1.0f) * 0.25f * 3.14159265f); // constant power
    const float g_r = vol * std::sin((pan + 1.0f) * 0.25f * 3.14159265f);

    if (!run) {
        render(l, r, 0, n, g_l, g_r); // let the last click ring out
        return;
    }

    const int beats = m_beats.load(std::memory_order_relaxed);
    const int subdiv = m_subdiv.load(std::memory_order_relaxed);
    const double bpm = std::clamp(m_bpm.load(std::memory_order_relaxed), 20.0, 400.0);
    const double tick = m_sr * 60.0 / bpm * (4.0 / m_unit.load(std::memory_order_relaxed)) / subdiv;
    const uint32_t acc = m_accents.load(std::memory_order_relaxed);
    const auto& set = m_samples[m_sound.load(std::memory_order_relaxed)];
    const bool silent_after = m_silent_after.load(std::memory_order_relaxed) &&
                              m_count_in_bars.load(std::memory_order_relaxed) > 0;

    // Tick positions are fractional samples; `frames` is the block length on that scale.
    const double frames = static_cast<double>(n);
    size_t i = 0;
    double until = m_until_next;
    while (until < frames) {
        const size_t at = std::max(i, static_cast<size_t>(until));
        render(l, r, i, at, g_l, g_r);
        i = at;

        if (m_beat >= beats) {
            m_beat = 0; // meter shortened while running
        }
        const bool counting = m_count_in_left > 0;
        const bool audible = counting || !silent_after;
        const Accent a = counting ? (m_beat == 0 ? STRONG : NORMAL) : accent_of(acc, m_beat);
        if (m_tick == 0) {
            m_beat_now.store(m_beat, std::memory_order_relaxed);
            m_count_in_now.store(counting, std::memory_order_relaxed);
            if (audible && a != MUTE) {
                trigger(set[a == STRONG ? 0 : 1], a == SOFT ? 0.45f : 1.0f);
            }
        } else if (audible && a != MUTE) {
            trigger(set[2], 0.55f);
        }

        if (++m_tick >= subdiv) {
            m_tick = 0;
            if (++m_beat >= beats) {
                m_beat = 0;
                if (m_count_in_left > 0) {
                    --m_count_in_left;
                }
            }
        }
        until += tick;
    }
    render(l, r, i, n, g_l, g_r);
    m_until_next = until - frames;
}
