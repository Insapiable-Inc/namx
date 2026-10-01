#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

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
        const size_t newest = newest_index();
        return samples[delay <= newest ? newest - delay : newest + samples.size() - delay];
    }

    // The signal `delay` samples behind the newest, linearly interpolated between the
    // two neighbouring samples. 0 <= delay < size() - 1.
    float interpolate(float delay) const {
        const size_t length = samples.size();
        float position = static_cast<float>(newest_index()) - delay;
        if (position < 0.0f) {
            position += static_cast<float>(length);
        }
        size_t i0 = static_cast<size_t>(position);
        if (i0 >= length) { // position rounded up to exactly `length`
            i0 -= length;
        }
        const size_t i1 = i0 + 1 == length ? 0 : i0 + 1;
        const float fraction = position - std::floor(position);
        return samples[i0] + (samples[i1] - samples[i0]) * fraction;
    }

private:
    size_t newest_index() const {
        return next == 0 ? samples.size() - 1 : next - 1;
    }

    std::vector<float> samples;
    size_t next = 0; // where the next push() writes
};
