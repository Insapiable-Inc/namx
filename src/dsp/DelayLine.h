#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

#include "Common.h"

namespace namx
{

// Circular buffer of past samples, the building block of every delay-based effect.
// Push one sample at a time, then read any of the last size() samples back by how far
// behind the newest one it is: delay 0 is the newest sample.
//
// resize() allocates and belongs on the GUI thread (or before audio starts); everything
// else is allocation-free and safe on the audio thread.
class DelayLine
{
public:
    // Allocates `length` zeroed samples.
    void resize(size_t length) {
        samples.assign(length, 0.0f);
        next = 0;
    }

    // Takes the other line's contents and position (same length). Allocation-free:
    // for bringing a right channel in line with the left on the audio thread.
    void copy_from(const DelayLine& other) {
        assert(samples.size() == other.samples.size());
        std::copy(other.samples.begin(), other.samples.end(), samples.begin());
        next = other.next;
    }

    // Zeroes the samples, keeping the length.
    void clear() {
        std::fill(samples.begin(), samples.end(), 0.0f);
        next = 0;
    }

    size_t size() const {
        return samples.size();
    }

    bool empty() const {
        return samples.empty();
    }

    // Stores `sample` as the newest, overwriting the oldest.
    void push(float sample) {
        assert(!samples.empty()); // resize() first: an effect processed before its rate
        samples[next] = sample;
        next = next + 1 == samples.size() ? 0 : next + 1;
    }

    // The sample the next push() overwrites: size() - 1 pushes behind the newest.
    // A line read with oldest() and then pushed is a fixed delay of size() samples.
    float oldest() const {
        return samples[next];
    }

    // The sample `delay` pushes behind the newest. delay < size().
    float at(size_t delay) const {
        assert(delay < samples.size());
        const size_t newest = newest_index();
        return samples[delay <= newest ? newest - delay : newest + samples.size() - delay];
    }

    // The signal `delay` samples behind the newest, linearly interpolated between the
    // two neighbouring samples. Clamped to 0 … size() - 2: a negative delay (a read
    // "from the future", which the pitch shifter's WSOLA offset can ask for) gets the
    // newest sample instead of wrapping around to the oldest.
    // The fraction is taken from `delay` itself, never from a buffer position: a float
    // index near 2^17 resolves only 1/64 of a sample, so the result would get coarser
    // (and change) with the buffer's length.
    float interpolate(float delay) const {
        delay = std::clamp(delay, 0.0f, static_cast<float>(samples.size() - 2));
        const float whole = std::floor(delay);
        const float fraction = delay - whole;
        const size_t newer = static_cast<size_t>(whole);
        const float a = at(newer);
        const float b = at(newer + 1);
        return a + (b - a) * fraction;
    }

private:
    size_t newest_index() const {
        return next == 0 ? samples.size() - 1 : next - 1;
    }

    vector<float> samples;
    size_t next = 0; // where the next push() writes
};

} // namespace namx
