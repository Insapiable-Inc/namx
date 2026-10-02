#include "WavReader.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <system_error>

namespace npl::wav
{

namespace
{

constexpr std::uint16_t FORMAT_PCM = 1;
constexpr std::uint16_t FORMAT_FLOAT = 3;
constexpr std::uint16_t FORMAT_ALAW = 6;
constexpr std::uint16_t FORMAT_MULAW = 7;
constexpr std::uint16_t FORMAT_EXTENSIBLE = 0xFFFE;

// Little-endian reads at a position the caller has already bounds-checked.
std::uint16_t u16(span<const std::byte> d, size_t at) {
    return static_cast<std::uint16_t>(std::to_integer<unsigned>(d[at]) |
                                      std::to_integer<unsigned>(d[at + 1]) << 8);
}

std::uint32_t u32(span<const std::byte> d, size_t at) {
    return static_cast<std::uint32_t>(u16(d, at)) | static_cast<std::uint32_t>(u16(d, at + 2))
                                                        << 16;
}

bool tag_is(span<const std::byte> d, size_t at, const char (&tag)[5]) {
    return std::memcmp(d.data() + at, tag, 4) == 0;
}

struct Format
{
    std::uint16_t code = 0; // PCM or FLOAT after resolving EXTENSIBLE
    std::uint16_t channels = 0;
    std::uint32_t rate = 0;
    std::uint16_t block_align = 0;
    std::uint16_t bits = 0;
};

expected<Format, string> parse_fmt(span<const std::byte> chunk) {
    if (chunk.size() < 16) {
        return std::unexpected("The file's format header is damaged.");
    }
    Format f;
    f.code = u16(chunk, 0);
    f.channels = u16(chunk, 2);
    f.rate = u32(chunk, 4);
    f.block_align = u16(chunk, 12);
    f.bits = u16(chunk, 14);
    if (f.code == FORMAT_EXTENSIBLE) {
        // cbSize, valid bits, channel mask, then the sub-format GUID whose first two
        // bytes are the plain format code.
        if (chunk.size() < 26) {
            return std::unexpected("The file's format header is damaged.");
        }
        f.code = u16(chunk, 24);
    }

    if (f.code == FORMAT_ALAW || f.code == FORMAT_MULAW) {
        return std::unexpected("A-law and µ-law WAV files are not supported; save the IR as "
                               "PCM or float.");
    }
    if (f.code != FORMAT_PCM && f.code != FORMAT_FLOAT) {
        return std::unexpected("Unsupported WAV encoding; save the IR as PCM or float.");
    }
    if (f.channels != 1) {
        return std::unexpected("The IR is not mono. Stereo IRs are not supported; convert "
                               "the file to mono.");
    }
    const bool pcm_ok = f.code == FORMAT_PCM && (f.bits == 16 || f.bits == 24 || f.bits == 32);
    const bool float_ok = f.code == FORMAT_FLOAT && (f.bits == 32 || f.bits == 64);
    if (!pcm_ok && !float_ok) {
        return std::unexpected("Unsupported sample size (" + std::to_string(f.bits) +
                               " bits); use 16, 24 or 32-bit PCM, or 32/64-bit float.");
    }
    if (f.block_align != f.bits / 8) {
        return std::unexpected("The file's format header is inconsistent (block size).");
    }
    if (f.rate < 1000 || f.rate > 768000) {
        return std::unexpected("Unsupported sample rate (" + std::to_string(f.rate) + " Hz).");
    }
    return f;
}

float decode(span<const std::byte> d, size_t at, const Format& f) {
    if (f.code == FORMAT_FLOAT) {
        if (f.bits == 32) {
            return std::bit_cast<float>(u32(d, at));
        }
        const std::uint64_t lo = u32(d, at);
        const std::uint64_t hi = u32(d, at + 4);
        return static_cast<float>(std::bit_cast<double>(lo | hi << 32));
    }
    switch (f.bits) {
        case 16:
            return static_cast<float>(static_cast<std::int16_t>(u16(d, at))) / 32768.0f;
        case 24: {
            // Three bytes, sign-extended through the top of a 32-bit word.
            const std::uint32_t v = static_cast<std::uint32_t>(u16(d, at)) << 8 |
                                    static_cast<std::uint32_t>(std::to_integer<unsigned>(d[at + 2]))
                                        << 24;
            return static_cast<float>(static_cast<std::int32_t>(v) >> 8) / 8388608.0f;
        }
        default: // 32-bit PCM; a positive scale (review item 11: 1 << 31 is negative)
            return static_cast<float>(static_cast<double>(static_cast<std::int32_t>(u32(d, at))) /
                                      2147483648.0);
    }
}

} // namespace

expected<MonoAudio, string> parse_mono(span<const std::byte> data) {
    if (data.size() < 12 || !tag_is(data, 0, "RIFF") || !tag_is(data, 8, "WAVE")) {
        return std::unexpected("Not a WAV file.");
    }

    // Walk the chunks. The RIFF size field isn't trusted: the bytes present are.
    expected<Format, string> format = std::unexpected("The file has no format header.");
    bool have_format = false;
    span<const std::byte> audio;
    bool have_audio = false;
    size_t at = 12;
    while (data.size() - at >= 8) {
        const std::uint32_t chunk_size = u32(data, at + 4);
        const size_t body = at + 8;
        if (chunk_size > data.size() - body) {
            return std::unexpected(tag_is(data, at, "data")
                                       ? "The file is truncated: its audio is incomplete."
                                       : "The file is damaged (a chunk runs past its end).");
        }
        const span<const std::byte> chunk = data.subspan(body, chunk_size);
        if (tag_is(data, at, "fmt ") && !have_format) {
            format = parse_fmt(chunk);
            have_format = true;
        } else if (tag_is(data, at, "data") && !have_audio) {
            audio = chunk;
            have_audio = true;
        }
        // Chunks are word-aligned: an odd size is followed by one pad byte.
        const size_t next = body + chunk_size + (chunk_size & 1u);
        if (next > data.size()) {
            break; // a missing final pad byte is harmless
        }
        at = next;
    }

    if (!format) {
        return std::unexpected(format.error());
    }
    if (!have_audio) {
        return std::unexpected("The file has no audio data.");
    }
    const Format& f = *format;
    // Whole samples only. Some DAWs write a stray byte after the last sample (a
    // 24-bit IR with 3n + 1 data bytes is common): it's ignored, never read.
    const size_t count = audio.size() / f.block_align;
    if (count == 0) {
        return std::unexpected("IR file contains no samples.");
    }

    MonoAudio out;
    out.sample_rate = f.rate;
    out.samples.resize(count);
    for (size_t i = 0; i < count; ++i) {
        out.samples[i] = decode(audio, i * f.block_align, f);
        if (!std::isfinite(out.samples[i])) { // NaN / inf would poison the audio for good
            return std::unexpected("The file contains invalid (non-numeric) samples.");
        }
    }
    return out;
}

expected<MonoAudio, string> read_mono(const std::filesystem::path& path) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        return std::unexpected("Cannot open the file.");
    }
    if (size > MAX_FILE_BYTES) {
        return std::unexpected("The file is too large for an impulse response.");
    }
    std::ifstream in(path, std::ios::binary);
    vector<std::byte> bytes(static_cast<size_t>(size));
    if (!in ||
        !in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
        return std::unexpected("Cannot read the file.");
    }
    return parse_mono(bytes);
}

} // namespace npl::wav
