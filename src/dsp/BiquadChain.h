#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

#include "Common.h"

namespace namx
{

// A fixed chain of up to N RBJ biquads (the Audio EQ Cookbook's designs), for the pedal
// voicings fitted to captures (Dd8Delay.h, Ce5Chorus.h): sections are described as data,
// designed once per sample rate, and run allocation-free on the audio thread.

enum class FilterKind : int
{
    NONE,
    LOW_PASS,
    HIGH_PASS,
    PEAK,
    LOW_SHELF,
};

// One biquad of a chain.
struct FilterSection
{
    FilterKind kind = FilterKind::NONE;
    float hz = 0.0f;
    float q = 0.707f;
    float gain_db = 0.0f;
};

template <size_t N>
using FilterChain = std::array<FilterSection, N>;

struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;

    // NONE (or a frequency past Nyquist) stays a pass-through.
    void design(const FilterSection& s, double sr) {
        *this = Biquad{};
        if (s.kind == FilterKind::NONE || s.hz >= 0.49 * sr) {
            return;
        }
        const double w = 2.0 * std::numbers::pi * s.hz / sr;
        const double c = std::cos(w);
        const double alpha = std::sin(w) / (2.0 * s.q);
        const double a = std::pow(10.0, s.gain_db / 40.0);
        double nb0 = 1.0, nb1 = 0.0, nb2 = 0.0, na0 = 1.0, na1 = 0.0, na2 = 0.0;
        switch (s.kind) {
            case FilterKind::LOW_PASS:
                nb0 = nb2 = (1.0 - c) / 2.0;
                nb1 = 1.0 - c;
                na0 = 1.0 + alpha;
                na1 = -2.0 * c;
                na2 = 1.0 - alpha;
                break;
            case FilterKind::HIGH_PASS:
                nb0 = nb2 = (1.0 + c) / 2.0;
                nb1 = -(1.0 + c);
                na0 = 1.0 + alpha;
                na1 = -2.0 * c;
                na2 = 1.0 - alpha;
                break;
            case FilterKind::PEAK:
                nb0 = 1.0 + alpha * a;
                nb1 = -2.0 * c;
                nb2 = 1.0 - alpha * a;
                na0 = 1.0 + alpha / a;
                na1 = -2.0 * c;
                na2 = 1.0 - alpha / a;
                break;
            case FilterKind::LOW_SHELF: {
                const double root = 2.0 * std::sqrt(a) * alpha;
                nb0 = a * ((a + 1.0) - (a - 1.0) * c + root);
                nb1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * c);
                nb2 = a * ((a + 1.0) - (a - 1.0) * c - root);
                na0 = (a + 1.0) + (a - 1.0) * c + root;
                na1 = -2.0 * ((a - 1.0) + (a + 1.0) * c);
                na2 = (a + 1.0) + (a - 1.0) * c - root;
                break;
            }
            case FilterKind::NONE:
                break;
        }
        b0 = static_cast<float>(nb0 / na0);
        b1 = static_cast<float>(nb1 / na0);
        b2 = static_cast<float>(nb2 / na0);
        a1 = static_cast<float>(na1 / na0);
        a2 = static_cast<float>(na2 / na0);
    }

    // |H| at `hz`, from the numerator and denominator polynomials.
    double magnitude(double hz, double sr) const {
        const double w = 2.0 * std::numbers::pi * hz / sr;
        const double num_re = b0 + b1 * std::cos(w) + b2 * std::cos(2.0 * w);
        const double num_im = -(b1 * std::sin(w) + b2 * std::sin(2.0 * w));
        const double den_re = 1.0 + a1 * std::cos(w) + a2 * std::cos(2.0 * w);
        const double den_im = -(a1 * std::sin(w) + a2 * std::sin(2.0 * w));
        return std::hypot(num_re, num_im) / std::hypot(den_re, den_im);
    }
};

// A chain's coefficients at one sample rate, scaled to unity at `unity_hz` (the voicing
// fits are relative to 1 kHz, where their levels were measured).
template <size_t N>
struct BiquadChain
{
    std::array<Biquad, N> sections{};
    size_t count = 0; // sections in use (packed from the front)
    float gain = 1.0f;

    void design(const FilterChain<N>& chain, double sr, double unity_hz = 1000.0) {
        count = 0;
        for (const FilterSection& s : chain) {
            if (s.kind != FilterKind::NONE) {
                sections[count++].design(s, sr);
            }
        }
        double magnitude = 1.0;
        for (size_t i = 0; i < count; ++i) {
            magnitude *= sections[i].magnitude(unity_hz, sr);
        }
        gain = static_cast<float>(1.0 / magnitude);
    }
};

// A chain's state (transposed direct form II, one per section).
template <size_t N>
struct BiquadChainState
{
    std::array<float, N> z1{};
    std::array<float, N> z2{};

    float process(float x, const BiquadChain<N>& k) {
        for (size_t i = 0; i < k.count; ++i) {
            const Biquad& b = k.sections[i];
            const float y = b.b0 * x + z1[i];
            z1[i] = b.b1 * x - b.a1 * y + z2[i];
            z2[i] = b.b2 * x - b.a2 * y;
            x = y;
        }
        return x * k.gain;
    }

    void reset() {
        z1.fill(0.0f);
        z2.fill(0.0f);
    }
};

} // namespace namx
