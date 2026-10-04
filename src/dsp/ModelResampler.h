#pragma once

// AudioDSPTools' ResamplingContainer (the resampler the official NAM plugin uses)
// comes from iPlug2 and expects two of iPlug2's names; this header supplies them and
// includes it. Include this, never <ResamplingContainer.h> directly — and only from a
// .cpp: its WDL dependencies include <windows.h> on Windows, whose macros (ABSOLUTE,
// RELATIVE, …) break any header that follows. NamProcessor.h only names ModelResampler.

#include <numbers>

namespace iplug
{
inline constexpr double PI = std::numbers::pi; // the vendored header expects this name
}

#ifndef DEFAULT_BLOCK_SIZE
#define DEFAULT_BLOCK_SIZE 512 // only a default argument; NamProcessor always passes one
#endif

#include <algorithm>
#include <cmath>
#include <memory>

#include <ResamplingContainer.h>

#include "Common.h"

namespace namx
{

// Runs a model at its training rate inside an engine running at another one
// (Lanczos, A = 12, one channel).
class ModelResampler : public dsp::ResamplingContainer<float, 1, 12>
{
public:
    explicit ModelResampler(double rendering_rate)
        : ResamplingContainer(rendering_rate)
        , native_rate(rendering_rate) {
    }

    // Hides the container's Reset (non-virtual; nothing calls it through the base).
    //
    // The container's warm-up pushes `latency` samples of silence from a scratch buffer
    // sized by `block_size` and pops the mid-stage samples into one sized by
    // ceil(block_size / ratio): with a block size of 0 (no audio yet), or a model rate
    // far below the engine's, the warm-up reads and writes past both buffers (audit
    // 2026-10-04 items 01/02). The buffers only ever need to be as large as the
    // largest block, so a block size that also covers the warm-up is harmless.
    void Reset(double engine_rate, int block_size) {
        // The same resamplers Reset() builds, to ask what the warm-up will need.
        const auto to_model = make_unique<LanczosResampler>(static_cast<float>(engine_rate),
                                                            static_cast<float>(native_rate));
        const auto to_engine = make_unique<LanczosResampler>(static_cast<float>(native_rate),
                                                             static_cast<float>(engine_rate));
        const size_t mid = to_engine->GetNumSamplesRequiredFor(1);
        const size_t latency = to_model->GetNumSamplesRequiredFor(mid);
        const double mid_in_engine_frames =
            std::ceil(static_cast<double>(mid) * engine_rate / native_rate);
        const size_t safe = std::max({static_cast<size_t>(std::max(block_size, 1)), latency,
                                      static_cast<size_t>(mid_in_engine_frames)});
        ResamplingContainer::Reset(engine_rate, static_cast<int>(safe));
    }

private:
    const double native_rate;
};

} // namespace namx
