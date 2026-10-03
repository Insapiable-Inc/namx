#pragma once

// AudioDSPTools' ResamplingContainer (the resampler the official NAM plugin uses)
// comes from iPlug2 and expects two of iPlug2's names; this header supplies them and
// includes it. Include this, never <ResamplingContainer.h> directly — and only from a
// .cpp: its WDL dependencies include <windows.h> on Windows, whose macros (ABSOLUTE,
// RELATIVE, …) break any header that follows. NamProcessor.h only names ModelResampler.

namespace iplug
{
inline constexpr double PI = 3.14159265358979323846;
}

#ifndef DEFAULT_BLOCK_SIZE
#define DEFAULT_BLOCK_SIZE 512 // only a default argument; NamProcessor always passes one
#endif

#include <ResamplingContainer.h>

namespace namx
{

// Runs a model at its training rate inside an engine running at another one
// (Lanczos, A = 12, one channel).
class ModelResampler : public dsp::ResamplingContainer<float, 1, 12>
{
public:
    using ResamplingContainer::ResamplingContainer;
};

} // namespace namx
