#include "Rt2Rotary.h"

namespace namx::rt2
{

namespace
{

constexpr double FIT_RATE = 48000.0;
constexpr double KERNEL_ZEROS = 6.0; // the resampling sinc's reach, zero crossings each side
constexpr double PI = std::numbers::pi;

double sinc(double x) {
    return std::abs(x) < 1e-12 ? 1.0 : std::sin(PI * x) / (PI * x);
}

// 4-point Lagrange interpolation at a fractional position between p1 (frac 0) and p2.
float lagrange(float p0, float p1, float p2, float p3, float f) {
    const float a = -f * (f - 1.0f) * (f - 2.0f) / 6.0f;
    const float b = (f + 1.0f) * (f - 1.0f) * (f - 2.0f) / 2.0f;
    const float c = -(f + 1.0f) * f * (f - 2.0f) / 2.0f;
    const float d = (f + 1.0f) * f * (f - 1.0f) / 6.0f;
    return a * p0 + b * p1 + c * p2 + d * p3;
}

// The crossover (4th-order Butterworth pairs) and the horn's shelf (S = 1), as fit_rt2.py.
constexpr std::array<float, 2> BUTTER4_Q{0.54119610f, 1.3065630f};
constexpr float SHELF_Q = 0.7071f;

size_t round_up8(size_t n) {
    return (n + 7) / 8 * 8;
}

// How far ahead a path reads at this rate: the filters' negative lags plus the kernel's reach.
size_t advance_for(int lag0, double rate) {
    if (rate == FIT_RATE) {
        return static_cast<size_t>(-lag0);
    }
    const double c = std::min(1.0, rate / FIT_RATE);
    return static_cast<size_t>(std::ceil((-lag0 + KERNEL_ZEROS / c) * rate / FIT_RATE));
}

} // namespace

Engine::Filter Engine::resample(std::span<const float> taps48, int lag0, double rate,
                                size_t advance) {
    std::vector<float> h;
    if (rate == FIT_RATE) {
        // At the fitted rate the taps are the measured filter itself.
        h.assign(taps48.begin(), taps48.end());
    } else {
        // The filter as a band-limited response sampled at the engine's rate: tap j sits
        // (j − advance)/rate after the path's read point, measured tap k at (lag0 + k)/48000.
        const double c = std::min(1.0, rate / FIT_RATE);
        const double reach = KERNEL_ZEROS / c;
        const double last = lag0 + static_cast<double>(taps48.size()) - 1.0 + reach;
        const auto count =
            static_cast<size_t>(std::ceil(static_cast<double>(advance) + last * rate / FIT_RATE)) +
            1;
        h.assign(count, 0.0f);
        for (size_t j = 0; j < count; ++j) {
            const double u =
                (static_cast<double>(j) - static_cast<double>(advance)) * FIT_RATE / rate - lag0;
            const auto k0 = static_cast<long>(std::ceil(u - reach));
            const auto k1 = static_cast<long>(std::floor(u + reach));
            double sum = 0.0;
            for (long k = std::max(0L, k0); k <= std::min(static_cast<long>(taps48.size()) - 1, k1);
                 ++k) {
                const double x = u - static_cast<double>(k);
                sum +=
                    taps48[static_cast<size_t>(k)] * c * sinc(c * x) * sinc(x * c / KERNEL_ZEROS);
            }
            h[j] = static_cast<float>(sum * FIT_RATE / rate);
        }
    }
    Filter f;
    f.taps.assign(round_up8(h.size()), 0.0f);
    // Reversed: dot() runs oldest to newest over the history.
    for (size_t i = 0; i < h.size(); ++i) {
        f.taps[f.taps.size() - 1 - i] = h[i];
    }
    return f;
}

Engine::LowRank Engine::make_bank(const Bank& b, size_t comps, size_t taps48, int lag0,
                                  size_t advance) const {
    LowRank bank;
    bank.rank = b.rank;
    bank.comps = comps;
    // Stored transposed (rank × components): the chunk's sums run over a row at a time.
    bank.gains_t.assign(comps * b.rank, 0.0f);
    for (size_t c = 0; c < comps; ++c) {
        for (size_t j = 0; j < b.rank; ++j) {
            bank.gains_t[j * comps + c] = b.gains[c * b.rank + j];
        }
    }
    for (size_t j = 0; j < b.rank; ++j) {
        bank.basis.push_back(resample({b.basis + j * taps48, taps48}, lag0, fs, advance));
    }
    bank.length = bank.basis.front().taps.size();
    return bank;
}

void Engine::build(size_t index, const ModeTables& t) {
    Mode& m = modes[index];
    m.paths.clear();
    m.banks.clear();
    const size_t direct_comps = 2 * DIRECT_ORDER + 1;
    const size_t refl_comps = 2 * REFLECTION_ORDER + 1;
    const size_t drum_comps = 2 * DRUM_ORDER + 1;
    // Banks: direct A, direct B, reflections A, reflections B, the drum (both outputs).
    const size_t direct_advance = advance_for(DIRECT_LAG0, fs);
    const size_t refl_advance = advance_for(REFLECTION_LAG0, fs);
    const size_t drum_advance = advance_for(DRUM_LAG0, fs);
    for (size_t o = 0; o < 2; ++o) {
        m.banks.push_back(
            make_bank(t.direct[o], direct_comps, DIRECT_TAPS, DIRECT_LAG0, direct_advance));
    }
    for (size_t o = 0; o < 2; ++o) {
        m.banks.push_back(make_bank(t.reflection[o], refl_comps * t.reflection_count,
                                    REFLECTION_TAPS, REFLECTION_LAG0, refl_advance));
    }
    m.banks.push_back(make_bank(t.drum, drum_comps, DRUM_TAPS, DRUM_LAG0, drum_advance));
    // Which components each bank takes (rows of `comps`, in path order: direct A, direct B,
    // the reflections, the drum) and which output it feeds.
    m.banks[0].first_row = 0;
    m.banks[0].output = 0;
    m.banks[1].first_row = direct_comps;
    m.banks[1].output = 1;
    m.banks[2].first_row = m.banks[3].first_row = 2 * direct_comps;
    m.banks[2].output = 0;
    m.banks[3].output = 1;
    m.banks[4].first_row = 2 * direct_comps + refl_comps * t.reflection_count;
    m.banks[4].output = -1;
    m.banks[4].drum = true;
    m.rows = m.banks[4].first_row + drum_comps;

    for (size_t o = 0; o < 2; ++o) {
        Path p;
        p.table = true;
        p.curve = t.direct_ms[o];
        p.cos_phi = static_cast<float>(std::cos(2.0 * PI * t.direct_phi[o]));
        p.sin_phi = static_cast<float>(std::sin(2.0 * PI * t.direct_phi[o]));
        p.order = DIRECT_ORDER;
        p.advance = direct_advance;
        m.paths.push_back(p);
    }
    for (size_t r = 0; r < t.reflection_count; ++r) {
        const Reflection& q = t.reflections[r];
        Path p;
        p.t_ms = q.t_ms;
        p.swing_ms = q.swing_ms;
        p.cos_phi = static_cast<float>(std::cos(2.0 * PI * q.phi));
        p.sin_phi = static_cast<float>(std::sin(2.0 * PI * q.phi));
        p.order = REFLECTION_ORDER;
        p.advance = refl_advance;
        m.paths.push_back(p);
    }
    {
        Path p;
        p.horn = false;
        p.table = true;
        p.curve = t.drum_ms;
        p.cos_phi = static_cast<float>(std::cos(2.0 * PI * t.drum_phi));
        p.sin_phi = static_cast<float>(std::sin(2.0 * PI * t.drum_phi));
        p.order = DRUM_ORDER;
        p.advance = drum_advance;
        m.paths.push_back(p);
    }
    for (size_t o = 0; o < 2; ++o) {
        m.horn_static[o] = resample({t.horn_static[o], STATIC_TAPS}, 0, fs, advance_for(0, fs));
    }
    m.drum_static = resample({t.drum_static, STATIC_TAPS}, 0, fs, advance_for(0, fs));
    m.static_length = m.drum_static.taps.size();
    // The crossover ahead of each rotor (on its line in process(); folded into its fixed filter
    // here) and the horn's shelf after it. The chains keep their absolute response.
    m.horn_cross.design({{{FilterKind::HIGH_PASS, t.horn_hp_hz, BUTTER4_Q[0]},
                          {FilterKind::HIGH_PASS, t.horn_hp_hz, BUTTER4_Q[1]}}},
                        fs);
    m.drum_cross.design({{{FilterKind::LOW_PASS, t.drum_lp_hz, BUTTER4_Q[0]},
                          {FilterKind::LOW_PASS, t.drum_lp_hz, BUTTER4_Q[1]}}},
                        fs);
    m.horn_cross.gain = m.drum_cross.gain = 1.0f;
    for (size_t o = 0; o < 2; ++o) {
        const HornTone& h = t.horn_tone[o];
        m.horn_shelf[o].design({{{FilterKind::HIGH_SHELF, h.shelf_hz, SHELF_Q, h.shelf_db}}}, fs);
        m.horn_shelf[o].gain = 1.0f;
    }
}

void Engine::prepare(double rate) {
    fs = rate;
    size_t sums_needed = 0;
    size_t static_length = 8;
    for (size_t i = 0; i < MODES.size(); ++i) {
        build(i, MODES[i]);
        size_t at = 0;
        for (LowRank& b : modes[i].banks) {
            b.history = at;
            at += b.rank * (b.length - 1 + CHUNK);
        }
        sums_needed = std::max(sums_needed, at);
        static_length = std::max(static_length, modes[i].static_length);
    }
    sums.assign(sums_needed, 0.0f);
    size_t rows = 0;
    for (const Mode& m : modes) {
        rows = std::max(rows, m.rows);
    }
    comps.assign(rows * CHUNK, 0.0f);
    horn_plain.assign(static_length - 1 + CHUNK, 0.0f);
    drum_plain.assign(static_length - 1 + CHUNK, 0.0f);
    // The input line: the longest read (reflections reach ~17 ms) plus the read-ahead.
    const auto need = static_cast<size_t>(std::ceil(25.0 * fs / 1000.0)) + 256;
    size_t size = 1;
    while (size < need) {
        size <<= 1;
    }
    horn_line.assign(size, 0.0f);
    drum_line.assign(size, 0.0f);
    mask = size - 1;
    drive.prepare(rate);
    reset();
}

void Engine::reset() {
    std::fill(horn_line.begin(), horn_line.end(), 0.0f);
    std::fill(drum_line.begin(), drum_line.end(), 0.0f);
    std::fill(sums.begin(), sums.end(), 0.0f);
    std::fill(horn_plain.begin(), horn_plain.end(), 0.0f);
    std::fill(drum_plain.begin(), drum_plain.end(), 0.0f);
    horn_cross_state.reset();
    drum_cross_state.reset();
    for (auto& h : shelf_state) {
        h.reset();
    }
    drive.reset();
    write = 0;
}

float Engine::curve_at(const float* curve, float turns) const {
    const float pos = turns * static_cast<float>(CURVE_POINTS);
    const auto i = static_cast<size_t>(pos);
    const float f = pos - static_cast<float>(i);
    const float a = curve[i % CURVE_POINTS];
    const float b = curve[(i + 1) % CURVE_POINTS];
    return a + f * (b - a);
}

void Engine::turn_rotors(const Settings& s, size_t n) {
    const float horn_target = s.fast ? s.fast_hz : s.slow_hz;
    const float drum_target = horn_target / (s.fast ? DRUM_RATIO_FAST : DRUM_RATIO_SLOW);
    const Inertia& horn_law = s.slow_ramps ? HORN_SLOW_RAMP : HORN_FAST_RAMP;
    const Inertia& drum_law = s.slow_ramps ? DRUM_SLOW_RAMP : DRUM_FAST_RAMP;
    const auto dt = static_cast<float>(1.0 / fs);
    const auto step = [dt](float speed, float target, const Inertia& law) {
        if (speed < target) {
            return law.up_s > 0.0f ? speed + (target - speed) * (1.0f - std::exp(-dt / law.up_s))
                                   : std::min(target, speed + law.up_slew * dt);
        }
        if (speed > target) {
            return law.down_s > 0.0f ? std::max(target, speed * std::exp(-dt / law.down_s))
                                     : std::max(target, speed - law.down_slew * dt);
        }
        return speed;
    };
    for (size_t i = 0; i < n; ++i) {
        horn_speed = step(horn_speed, horn_target, horn_law);
        drum_speed = step(drum_speed, drum_target, drum_law);
        horn_angle += horn_speed / fs;
        drum_angle += drum_speed / fs;
        horn_angle -= std::floor(horn_angle);
        drum_angle -= std::floor(drum_angle);
        horn_turns[i] = static_cast<float>(horn_angle);
        drum_turns[i] = static_cast<float>(drum_angle);
        horn_cos[i] = static_cast<float>(std::cos(2.0 * PI * horn_angle));
        horn_sin[i] = static_cast<float>(std::sin(2.0 * PI * horn_angle));
        drum_cos[i] = static_cast<float>(std::cos(2.0 * PI * drum_angle));
        drum_sin[i] = static_cast<float>(std::sin(2.0 * PI * drum_angle));
    }
}

float Engine::drive_sample(float x) {
    constexpr float REFERENCE = 0.501187f; // REFERENCE_DBFS as a gain
    constexpr float CEILING = 1.29420f; // INPUT_CEILING_DB as a gain
    return drive.process(std::clamp(x / REFERENCE, -CEILING, CEILING)) * REFERENCE;
}

void Engine::process(const float* in, float* a, float* b, size_t n, const Settings& s) {
    // audio thread
    if (horn_line.empty()) {
        return;
    }
    if (s.mode != active) {
        active = s.mode;
        std::fill(sums.begin(), sums.end(), 0.0f);
        horn_cross_state.reset();
        drum_cross_state.reset();
        for (auto& h : shelf_state) {
            h.reset();
        }
        drive.reset();
    }
    Mode& m = modes[static_cast<size_t>(active)];
    const size_t outputs = b ? 2 : 1;
    const auto rate_ms = static_cast<float>(fs / 1000.0);
    drive.set(active, s.drive);
    const size_t static_len = m.static_length;
    for (size_t done = 0; done < n;) {
        const size_t count = std::min(CHUNK, n - done);
        for (size_t i = 0; i < count; ++i) {
            x_chunk[i] = drive_sample(in[done + i]);
        }
        turn_rotors(s, count);
        // A path reads `advance` samples ahead, so its angle is the rotor's that much later.
        for (size_t k = 0; k < m.paths.size(); ++k) {
            const Path& p = m.paths[k];
            const double ahead =
                (p.horn ? horn_speed : drum_speed) * static_cast<double>(p.advance) / fs;
            ahead_turns[k] = static_cast<float>(ahead);
            ahead_cos[k] = static_cast<float>(std::cos(2.0 * PI * ahead));
            ahead_sin[k] = static_cast<float>(std::sin(2.0 * PI * ahead));
        }
        // Each path's components for the whole chunk (the line fills as it goes): row c of
        // `comps` is component c, sample by sample.
        for (size_t i = 0; i < count; ++i) {
            const float to_horn = horn_cross_state.process(x_chunk[i], m.horn_cross);
            const float to_drum = drum_cross_state.process(x_chunk[i], m.drum_cross);
            horn_line[write] = to_horn;
            drum_line[write] = to_drum;
            horn_plain[static_len - 1 + i] = to_horn;
            drum_plain[static_len - 1 + i] = to_drum;
            size_t row = 0;
            for (size_t k = 0; k < m.paths.size(); ++k) {
                const Path& p = m.paths[k];
                const float cr = p.horn ? horn_cos[i] : drum_cos[i];
                const float sr = p.horn ? horn_sin[i] : drum_sin[i];
                // The rotor's angle at the read: θ + ahead; then cos and sin of 2π(θ − φ).
                const float c0 = cr * ahead_cos[k] - sr * ahead_sin[k];
                const float s0 = sr * ahead_cos[k] + cr * ahead_sin[k];
                const float c1 = c0 * p.cos_phi + s0 * p.sin_phi;
                const float s1 = s0 * p.cos_phi - c0 * p.sin_phi;
                float turns = (p.horn ? horn_turns[i] : drum_turns[i]) + ahead_turns[k];
                turns -= std::floor(turns);
                const float ms = p.table ? curve_at(p.curve, turns) : p.t_ms - p.swing_ms * c1;
                const float delay = std::max(2.0f, ms * rate_ms - static_cast<float>(p.advance));
                const auto whole = static_cast<size_t>(delay);
                const float frac = delay - static_cast<float>(whole);
                // Newest at write; the read sits between `whole` and `whole + 1` samples back.
                const size_t at = write - whole;
                const std::vector<float>& line = p.horn ? horn_line : drum_line;
                const float v = lagrange(line[(at + 1) & mask], line[at & mask],
                                         line[(at - 1) & mask], line[(at - 2) & mask], frac);
                float* col = &comps[row * CHUNK + i];
                col[0] = v;
                col[CHUNK] = v * c1;
                col[2 * CHUNK] = v * s1;
                if (p.order >= 2) {
                    const float c2 = c1 * c1 - s1 * s1;
                    const float s2 = 2.0f * s1 * c1;
                    col[3 * CHUNK] = v * c2;
                    col[4 * CHUNK] = v * s2;
                    if (p.order >= 3) {
                        col[5 * CHUNK] = v * (c2 * c1 - s2 * s1);
                        col[6 * CHUNK] = v * (s2 * c1 + c2 * s1);
                    }
                }
                row += 2 * p.order + 1;
            }
            write = (write + 1) & mask;
        }
        // Each bank's rank sums: its gains (rank × its components) times the components.
        for (size_t bi = 0; bi < m.banks.size(); ++bi) {
            LowRank& bank = m.banks[bi];
            if (!b && bank.output == 1) {
                continue; // mono: Output A alone
            }
            const size_t stride = bank.length - 1 + CHUNK;
            for (size_t j = 0; j < bank.rank; ++j) {
                float* out_row = &sums[bank.history + j * stride] + (bank.length - 1);
                std::fill(out_row, out_row + count, 0.0f);
                const float* g = &bank.gains_t[j * bank.comps];
                for (size_t c = 0; c < bank.comps; ++c) {
                    const float gc = g[c];
                    const float* in_row = &comps[(bank.first_row + c) * CHUNK];
                    for (size_t i = 0; i < count; ++i) {
                        out_row[i] += gc * in_row[i];
                    }
                }
            }
        }
        // Filter every bank's sums into each output's horn and drum sums.
        std::array<std::array<float, CHUNK>, 2> horn{}, drum{};
        for (size_t bi = 0; bi < m.banks.size(); ++bi) {
            const LowRank& bank = m.banks[bi];
            const size_t stride = bank.length - 1 + CHUNK;
            if (!b && bank.output == 1) {
                continue;
            }
            std::array<float, CHUNK> acc{};
            for (size_t j = 0; j < bank.rank; ++j) {
                const float* hist = &sums[bank.history + j * stride];
                const float* taps = bank.basis[j].taps.data();
                for (size_t i = 0; i < count; ++i) {
                    acc[i] += dot(taps, hist + i, bank.length);
                }
            }
            for (size_t o = 0; o < outputs; ++o) {
                if (bank.output >= 0 && static_cast<size_t>(bank.output) != o) {
                    continue;
                }
                auto& sum = bank.drum ? drum[o] : horn[o];
                for (size_t i = 0; i < count; ++i) {
                    sum[i] += acc[i];
                }
            }
        }
        // Each rotor's fixed filter on its side of the crossover (the drum's is the same on both
        // outputs); the horn's shelf on its whole sound, per output.
        std::array<float, CHUNK> drum_all{};
        for (size_t i = 0; i < count; ++i) {
            drum_all[i] =
                drum[0][i] + dot(m.drum_static.taps.data(), drum_plain.data() + i, static_len);
        }
        std::array<float*, 2> outs{a, b};
        for (size_t o = 0; o < outputs; ++o) {
            const float* taps = m.horn_static[o].taps.data();
            for (size_t i = 0; i < count; ++i) {
                const float horn_all = shelf_state[o].process(
                    horn[o][i] + dot(taps, horn_plain.data() + i, static_len), m.horn_shelf[o]);
                outs[o][done + i] = s.level * (s.horn_gain * horn_all + s.drum_gain * drum_all[i]);
            }
        }
        // Keep each buffer's last (length − 1) values for the next chunk.
        for (const LowRank& bank : m.banks) {
            const size_t stride = bank.length - 1 + CHUNK;
            for (size_t j = 0; j < bank.rank; ++j) {
                float* row = &sums[bank.history + j * stride];
                std::memmove(row, row + count, (bank.length - 1) * sizeof(float));
            }
        }
        std::memmove(horn_plain.data(), horn_plain.data() + count,
                     (static_len - 1) * sizeof(float));
        std::memmove(drum_plain.data(), drum_plain.data() + count,
                     (static_len - 1) * sizeof(float));
        done += count;
    }
}

} // namespace namx::rt2
