#pragma once

#include <cstdint>
#include <filesystem>

#include "Common.h"

namespace npl::wav
{

// Mono WAV reader for impulse responses: PCM 16 / 24 / 32-bit, IEEE float 32 / 64-bit,
// plain or WAVE_FORMAT_EXTENSIBLE. Strict by design — every length is checked against
// the bytes actually present before anything is allocated or decoded, so a damaged or
// hostile file is an error message, never an out-of-bounds read or write. Replaces
// AudioDSPTools' loader for the GUI (code review items 01, 10, 11).
struct MonoAudio
{
    vector<float> samples; // -1…1 for PCM; floats as stored
    double sample_rate = 0.0;
};

// Largest file read_mono() accepts: far beyond any IR, small enough to read whole.
inline constexpr std::uintmax_t MAX_FILE_BYTES = 64ull * 1024 * 1024;

// Reads and decodes a file; the error says why it can't be used.
expected<MonoAudio, string> read_mono(const std::filesystem::path& path);

// Decodes a whole file already in memory (what read_mono() does after reading it).
expected<MonoAudio, string> parse_mono(span<const std::byte> data);

} // namespace npl::wav
