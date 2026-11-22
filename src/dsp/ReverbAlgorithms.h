#pragma once

#include <algorithm>
#include <cstddef>

#include "Common.h"
#include "DelayLine.h"

namespace namx
{

// Reverb building blocks (the RV-6's diffusers and early smear, Rv6Reverb.h). NAM-X's own
// Plate (Dattorro) and Hall/Room (feedback delay networks) tanks lived here until 2026-11-03
// (git history). prepare() allocates (GUI thread / audio stopped); the rest doesn't.

namespace reverb_tanks
{

// Lattice all-pass: w = x + g·d, y = d − g·w, d ← w. Delay of `length` samples.
struct AllPass
{
    DelayLine line;
    size_t length = 1;
    void prepare(size_t new_length) {
        length = std::max<size_t>(1, new_length);
        line.resize(length + 1);
    }
    void clear() {
        line.clear();
    }
    float process(float x, float g) {
        const float d = line.at(length - 1);
        const float w = x + g * d;
        line.push(w);
        return d - g * w;
    }
    // A tap inside the all-pass's own delay (`delay` samples behind its newest write).
    float tap(size_t delay) const {
        return line.at(delay);
    }
};

} // namespace reverb_tanks
} // namespace namx
