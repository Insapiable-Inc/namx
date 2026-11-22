#pragma once

#include <memory>

namespace namx::rv6
{

// The capture-fitted mono octave paths and stereo output of the RV-6 Shimmer mode.
// prepare() runs with audio stopped. The clock, delay lines and convolution histories
// belong to this instance; only the immutable measured responses are shared.
class Shimmer
{
public:
    Shimmer();
    ~Shimmer();
    void prepare(double rate);
    void reset() noexcept;
    void set_controls(float time, float tone) noexcept;
    void process(float input, float& a, float& b) noexcept;

private:
    struct State;
    std::unique_ptr<State> state;
};

} // namespace namx::rv6
