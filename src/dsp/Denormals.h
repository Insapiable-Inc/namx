#pragma once

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>

#include "Common.h"

namespace namx
{

#endif

// Flushes denormals to zero on the calling thread (FTZ + DAZ in MXCSR). Reverb/delay
// feedback tails and NAM recurrent state decay into the denormal range, where each
// float op costs 10-100x. MXCSR is per-thread: call this on the audio thread itself.
// Same effect as _MM_SET_FLUSH_ZERO_MODE(ON) + _MM_SET_DENORMALS_ZERO_MODE(ON), whose
// macros trip -Wsign-conversion.
inline void enable_flush_to_zero() {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    constexpr unsigned int FLUSH_TO_ZERO = 0x8000; // MXCSR bit 15 (FTZ)
    constexpr unsigned int DENORMALS_ARE_ZERO = 0x0040; // MXCSR bit 6 (DAZ)
    _mm_setcsr(_mm_getcsr() | FLUSH_TO_ZERO | DENORMALS_ARE_ZERO);
#endif
}

} // namespace namx
