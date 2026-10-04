#pragma once

#include <signalsmith-stretch/signalsmith-stretch.h>

#include "Common.h"

namespace namx
{

// High-quality pitch shifter for the tails (the Reverb's and the Delay's Shimmer): Signalsmith
// Stretch (MIT), an STFT shifter, at its default 120 ms block — accurate pitch even in
// the bass, at the cost of latency (latency_samples()), which inside a reverb or a delay
// repeat doesn't matter (the Delay compensates for it). Mono, one sample at a time;
// the computation is spread across samples (splitComputation), so no block spikes.
// Not for live Transpose: at playable block sizes its bass comes out off-pitch
// (DSP_RESEARCH.md §1). prepare() allocates (GUI thread / audio stopped); process()
// and reset() don't.
class StretchShifter
{
public:
    void prepare(double sr, float semitones) {
        stretch.presetDefault(1, static_cast<float>(sr), true);
        stretch.setTransposeSemitones(semitones);
        latency = static_cast<size_t>(stretch.inputLatency() + stretch.outputLatency());
        reset();
    }

    void reset() {
        stretch.reset();
    }

    // Input to output delay of the shifted signal.
    size_t latency_samples() const {
        return latency;
    }

    float process(float x) {
        float out = 0.0f;
        float* in_ptr = &x;
        float* out_ptr = &out;
        stretch.process(&in_ptr, 1, &out_ptr, 1);
        return out;
    }

private:
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    size_t latency = 0;
};

} // namespace namx
