#pragma once

// AudioDSPTools' ResamplingContainer (the resampler the official NAM plugin uses)
// comes from iPlug2 and expects two of iPlug2's names; this header supplies them and
// includes it. Include this, never <ResamplingContainer.h> directly.

namespace iplug
{
inline constexpr double PI = 3.14159265358979323846;
}

#ifndef DEFAULT_BLOCK_SIZE
#define DEFAULT_BLOCK_SIZE 512 // only a default argument; NamProcessor always passes one
#endif

#include <ResamplingContainer.h>
